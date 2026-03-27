/*! @file
    @brief Implements the default latest-wins completion session.
*/

#include "qompi/completion_session.h"

#include "qompi/completion_backend.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace qompi
{

namespace
{

std::string generated_request_id()
{
    static std::atomic<std::uint64_t> next_request_id{1};
    return std::string("qompi-") + std::to_string(next_request_id.fetch_add(1));
}

/**
 * No-op sink used when the caller does not supply an explicit consumer.
 */
class null_completion_sink_t final : public completion_sink_t
{
public:
    /** @copydoc completion_sink_t::on_chunk */
    void on_chunk(const completion_chunk_t &chunk) override
    {
        (void)chunk;
    }

    /** @copydoc completion_sink_t::on_completed */
    void on_completed(const completion_result_t &result) override
    {
        (void)result;
    }

    /** @copydoc completion_sink_t::on_failed */
    void on_failed(const completion_error_t &error) override
    {
        (void)error;
    }
};

/**
 * Session-owned operation wrapper that tracks lifecycle state independently from provider code.
 */
class session_completion_operation_t final : public completion_operation_t
{
public:
    /**
     * Creates the operation wrapper.
     * @param request_id Stable request identifier.
     */
    explicit session_completion_operation_t(std::string request_id)
        : request_id_value(std::move(request_id))
    {
    }

    /** @copydoc completion_operation_t::request_id */
    std::string request_id() const override
    {
        return this->request_id_value;
    }

    /** @copydoc completion_operation_t::state */
    completion_operation_state_t state() const override
    {
        return this->current_state.load();
    }

    /** @copydoc completion_operation_t::cancel */
    void cancel() override
    {
        const completion_operation_state_t previous_state =
            this->current_state.exchange(completion_operation_state_t::CANCELLED);
        if (previous_state == completion_operation_state_t::COMPLETED ||
            previous_state == completion_operation_state_t::FAILED ||
            previous_state == completion_operation_state_t::CANCELLED)
        {
            return;
        }

        std::shared_ptr<completion_operation_t> backend_operation_copy;
        {
            std::scoped_lock lock(this->mutex);
            backend_operation_copy = this->backend_operation;
        }
        if (backend_operation_copy != nullptr)
        {
            backend_operation_copy->cancel();
        }
    }

    /**
     * Attaches the provider-owned operation handle.
     * @param backend_operation_value Provider handle.
     */
    void set_backend_operation(std::shared_ptr<completion_operation_t> backend_operation_value)
    {
        if (backend_operation_value == nullptr)
        {
            return;
        }

        const completion_operation_state_t current_state = this->state();
        if (current_state == completion_operation_state_t::CANCELLED)
        {
            backend_operation_value->cancel();
            return;
        }

        {
            std::scoped_lock lock(this->mutex);
            this->backend_operation = backend_operation_value;
        }

        if (this->state() == completion_operation_state_t::CANCELLED)
        {
            backend_operation_value->cancel();
        }
    }

    /**
     * Marks the operation as started when still active.
     * @return True when the transition was applied.
     */
    bool mark_started()
    {
        return this->transition_to(completion_operation_state_t::STARTED);
    }

    /**
     * Marks the operation as streaming when still active.
     * @return True when the transition was applied.
     */
    bool mark_streaming()
    {
        return this->transition_to(completion_operation_state_t::STREAMING);
    }

    /**
     * Marks the operation as completed when still active.
     * @return True when the transition was applied.
     */
    bool mark_completed()
    {
        return this->transition_to(completion_operation_state_t::COMPLETED);
    }

    /**
     * Marks the operation as failed when still active.
     * @return True when the transition was applied.
     */
    bool mark_failed()
    {
        return this->transition_to(completion_operation_state_t::FAILED);
    }

private:
    bool transition_to(const completion_operation_state_t target_state)
    {
        completion_operation_state_t expected_state = this->current_state.load();
        while (true)
        {
            if (expected_state == completion_operation_state_t::COMPLETED ||
                expected_state == completion_operation_state_t::FAILED ||
                expected_state == completion_operation_state_t::CANCELLED)
            {
                return false;
            }
            if (this->current_state.compare_exchange_weak(expected_state, target_state) == true)
            {
                return true;
            }
        }
    }

    std::string request_id_value;
    std::atomic<completion_operation_state_t> current_state{completion_operation_state_t::QUEUED};
    mutable std::mutex mutex;
    std::shared_ptr<completion_operation_t> backend_operation;
};

class latest_wins_completion_session_t;

/**
 * Filters backend callbacks so cancelled or superseded operations cannot leak stale results.
 */
class filtering_completion_sink_t final : public completion_sink_t
{
public:
    /**
     * Creates the filtering sink.
     * @param session Owning session.
     * @param generation Request generation captured at dispatch time.
     * @param operation Wrapped operation.
     * @param downstream Final consumer sink.
     */
    filtering_completion_sink_t(latest_wins_completion_session_t *session,
                                std::uint64_t generation,
                                std::shared_ptr<session_completion_operation_t> operation,
                                std::shared_ptr<completion_sink_t> downstream)
        : session(session), generation(generation), operation(std::move(operation)),
          downstream(std::move(downstream))
    {
    }

    /** @copydoc completion_sink_t::on_started */
    void on_started(const std::string &request_id) override;

    /** @copydoc completion_sink_t::on_debug_event */
    void on_debug_event(const std::string &stage, const std::string &message) override;

    /** @copydoc completion_sink_t::on_chunk */
    void on_chunk(const completion_chunk_t &chunk) override;

    /** @copydoc completion_sink_t::on_completed */
    void on_completed(const completion_result_t &result) override;

    /** @copydoc completion_sink_t::on_failed */
    void on_failed(const completion_error_t &error) override;

private:
    bool should_forward() const;

    latest_wins_completion_session_t *session = nullptr;
    std::uint64_t generation = 0;
    std::shared_ptr<session_completion_operation_t> operation;
    std::shared_ptr<completion_sink_t> downstream;
};

/**
 * Default session implementation with latest-wins cancellation semantics.
 */
class latest_wins_completion_session_t final : public completion_session_t
{
public:
    /**
     * Creates the session.
     * @param backend Shared backend used for provider work.
     */
    explicit latest_wins_completion_session_t(std::shared_ptr<completion_backend_t> backend)
        : backend(std::move(backend))
    {
    }

    /** @copydoc completion_session_t::request_completion */
    std::shared_ptr<completion_operation_t>
    request_completion(const completion_request_t &request, const completion_options_t &options,
                       std::shared_ptr<completion_sink_t> sink) override
    {
        completion_request_t normalized_request = request;
        if (normalized_request.request_id.empty() == true)
        {
            normalized_request.request_id = generated_request_id();
        }
        if (sink == nullptr)
        {
            sink = std::make_shared<null_completion_sink_t>();
        }

        const auto operation =
            std::make_shared<session_completion_operation_t>(normalized_request.request_id);

        std::shared_ptr<session_completion_operation_t> previous_operation;
        std::uint64_t current_generation = 0;
        {
            std::scoped_lock lock(this->mutex);
            previous_operation = this->active_operation;
            this->active_operation = operation;
            this->active_generation += 1;
            current_generation = this->active_generation;
        }

        if (previous_operation != nullptr)
        {
            previous_operation->cancel();
        }

        if (this->backend == nullptr)
        {
            completion_error_t error;
            error.code = 1;
            error.message = "No completion backend configured.";
            operation->mark_failed();
            sink->on_failed(error);
            return operation;
        }

        const auto filtering_sink = std::make_shared<filtering_completion_sink_t>(
            this, current_generation, operation, std::move(sink));
        const std::shared_ptr<completion_operation_t> backend_operation =
            this->backend->start_completion(normalized_request, options, filtering_sink);
        operation->set_backend_operation(backend_operation);
        return operation;
    }

    /** @copydoc completion_session_t::cancel_active */
    void cancel_active() override
    {
        std::shared_ptr<session_completion_operation_t> active_operation_copy;
        {
            std::scoped_lock lock(this->mutex);
            active_operation_copy = this->active_operation;
        }
        if (active_operation_copy != nullptr)
        {
            active_operation_copy->cancel();
        }
    }

    /**
     * Returns whether one callback still belongs to the current active operation.
     * @param generation Request generation captured by the callback bridge.
     * @param operation Candidate operation.
     * @return True when the callback may still be forwarded.
     */
    bool is_current(const std::uint64_t generation,
                    const std::shared_ptr<session_completion_operation_t> &operation) const
    {
        std::scoped_lock lock(this->mutex);
        return generation == this->active_generation && this->active_operation == operation;
    }

private:
    std::shared_ptr<completion_backend_t> backend;
    mutable std::mutex mutex;
    std::uint64_t active_generation = 0;
    std::shared_ptr<session_completion_operation_t> active_operation;
};

bool filtering_completion_sink_t::should_forward() const
{
    if (this->session == nullptr || this->operation == nullptr)
    {
        return false;
    }
    return this->session->is_current(this->generation, this->operation) == true &&
           this->operation->state() != completion_operation_state_t::CANCELLED;
}

void filtering_completion_sink_t::on_started(const std::string &request_id)
{
    if (this->operation != nullptr)
    {
        this->operation->mark_started();
    }
    if (this->should_forward() == true && this->downstream != nullptr)
    {
        this->downstream->on_started(request_id);
    }
}

void filtering_completion_sink_t::on_debug_event(const std::string &stage,
                                                 const std::string &message)
{
    if (this->should_forward() == true && this->downstream != nullptr)
    {
        this->downstream->on_debug_event(stage, message);
    }
}

void filtering_completion_sink_t::on_chunk(const completion_chunk_t &chunk)
{
    if (this->operation != nullptr)
    {
        this->operation->mark_streaming();
    }
    if (this->should_forward() == true && this->downstream != nullptr)
    {
        this->downstream->on_chunk(chunk);
    }
}

void filtering_completion_sink_t::on_completed(const completion_result_t &result)
{
    if (this->operation == nullptr || this->operation->mark_completed() == false)
    {
        return;
    }
    if (this->should_forward() == false || this->downstream == nullptr)
    {
        return;
    }

    completion_result_t final_result = result;
    if (final_result.request_id.empty() == true)
    {
        final_result.request_id = this->operation->request_id();
    }
    this->downstream->on_completed(final_result);
}

void filtering_completion_sink_t::on_failed(const completion_error_t &error)
{
    if (this->operation == nullptr || this->operation->mark_failed() == false)
    {
        return;
    }
    if (this->should_forward() == false || this->downstream == nullptr)
    {
        return;
    }
    this->downstream->on_failed(error);
}

}  // namespace

std::shared_ptr<completion_session_t>
create_latest_wins_completion_session(std::shared_ptr<completion_backend_t> backend)
{
    return std::make_shared<latest_wins_completion_session_t>(std::move(backend));
}

}  // namespace qompi
