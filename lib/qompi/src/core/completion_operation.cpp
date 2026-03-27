/*! @file
    @brief Implements reusable completion-operation helpers.
*/

#include "qompi/completion_operation.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace qompi
{

namespace
{

/**
 * Simple callback-backed operation used by delegate-style integrations.
 */
class callback_completion_operation_t final : public completion_operation_t
{
public:
    /**
     * Creates the operation.
     * @param request_id Correlation id for the operation.
     * @param cancel_callback Callback invoked at most once on cancellation.
     */
    explicit callback_completion_operation_t(std::string request_id,
                                             std::function<void()> cancel_callback)
        : request_id_value(std::move(request_id)), cancel_callback(std::move(cancel_callback))
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
        return this->state_value.load(std::memory_order_acquire);
    }

    /** @copydoc completion_operation_t::cancel */
    void cancel() override
    {
        const completion_operation_state_t previous_state = this->state_value.exchange(
            completion_operation_state_t::CANCELLED, std::memory_order_acq_rel);
        if (previous_state == completion_operation_state_t::CANCELLED ||
            previous_state == completion_operation_state_t::COMPLETED ||
            previous_state == completion_operation_state_t::FAILED)
        {
            return;
        }

        std::function<void()> callback;
        {
            const std::scoped_lock lock(this->mutex);
            callback = std::move(this->cancel_callback);
        }
        if (static_cast<bool>(callback) == true)
        {
            callback();
        }
    }

private:
    std::string request_id_value;
    mutable std::mutex mutex;
    std::function<void()> cancel_callback;
    std::atomic<completion_operation_state_t> state_value{completion_operation_state_t::STARTED};
};

}  // namespace

bool completion_operation_t::is_terminal() const
{
    const completion_operation_state_t current_state = this->state();
    return current_state == completion_operation_state_t::COMPLETED ||
           current_state == completion_operation_state_t::FAILED ||
           current_state == completion_operation_state_t::CANCELLED;
}

std::shared_ptr<completion_operation_t>
create_callback_completion_operation(std::string request_id, std::function<void()> cancel_callback)
{
    return std::make_shared<callback_completion_operation_t>(std::move(request_id),
                                                             std::move(cancel_callback));
}

}  // namespace qompi
