/*! @file
    @brief Regression tests for qompi latest-wins session behavior.
*/

#include <qompi/completion_backend.h>
#include <qompi/completion_engine.h>
#include <qompi/completion_session.h>

#include <QtTest>

#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace qompi
{

namespace
{

class fake_backend_operation_t final : public completion_operation_t
{
public:
    explicit fake_backend_operation_t(std::string request_id)
        : request_id_value(std::move(request_id))
    {
    }

    std::string request_id() const override
    {
        return this->request_id_value;
    }

    completion_operation_state_t state() const override
    {
        return this->current_state.load();
    }

    void cancel() override
    {
        this->cancel_count.fetch_add(1);
        this->current_state.store(completion_operation_state_t::CANCELLED);
    }

    void set_state(const completion_operation_state_t new_state)
    {
        this->current_state.store(new_state);
    }

    int cancelled_times() const
    {
        return this->cancel_count.load();
    }

private:
    std::string request_id_value;
    std::atomic<completion_operation_state_t> current_state{completion_operation_state_t::QUEUED};
    std::atomic<int> cancel_count{0};
};

class recording_completion_sink_t final : public completion_sink_t
{
public:
    void on_started(const std::string &request_id) override
    {
        this->started_ids.push_back(request_id);
    }

    void on_chunk(const completion_chunk_t &chunk) override
    {
        this->chunks.push_back(chunk.stable_text);
    }

    void on_completed(const completion_result_t &result) override
    {
        this->completed_texts.push_back(result.text);
    }

    void on_failed(const completion_error_t &error) override
    {
        this->failed_messages.push_back(error.message);
    }

    std::vector<std::string> started_ids;
    std::vector<std::string> chunks;
    std::vector<std::string> completed_texts;
    std::vector<std::string> failed_messages;
};

class fake_completion_backend_t final : public completion_backend_t
{
public:
    struct pending_request_t
    {
        completion_request_t request;
        std::shared_ptr<completion_sink_t> sink;
        std::shared_ptr<fake_backend_operation_t> operation;
    };

    std::shared_ptr<completion_operation_t>
    start_completion(const completion_request_t &request, const completion_options_t &options,
                     std::shared_ptr<completion_sink_t> sink) override
    {
        (void)options;
        auto operation = std::make_shared<fake_backend_operation_t>(request.request_id);
        operation->set_state(completion_operation_state_t::STARTED);
        this->pending_requests.push_back({request, std::move(sink), operation});
        return operation;
    }

    void emit_started(const int index)
    {
        this->pending_requests.at(static_cast<std::size_t>(index))
            .sink->on_started(
                this->pending_requests.at(static_cast<std::size_t>(index)).request.request_id);
    }

    void emit_chunk(const int index, const std::string &text)
    {
        completion_chunk_t chunk;
        chunk.text_delta = text;
        chunk.stable_text = text;
        this->pending_requests.at(static_cast<std::size_t>(index)).sink->on_chunk(chunk);
    }

    void emit_completed(const int index, const std::string &text)
    {
        completion_result_t result;
        result.request_id =
            this->pending_requests.at(static_cast<std::size_t>(index)).request.request_id;
        result.text = text;
        this->pending_requests.at(static_cast<std::size_t>(index)).sink->on_completed(result);
        this->pending_requests.at(static_cast<std::size_t>(index))
            .operation->set_state(completion_operation_state_t::COMPLETED);
    }

    std::vector<pending_request_t> pending_requests;
};

}  // namespace

class tst_qompi_completion_session_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * A new request must cancel the previous inflight operation.
     */
    void latest_wins_cancels_previous_request();

    /**
     * Provider callbacks from a superseded request must be suppressed.
     */
    void stale_results_are_suppressed_after_newer_request();
};

void tst_qompi_completion_session_t::latest_wins_cancels_previous_request()
{
    auto backend = std::make_shared<fake_completion_backend_t>();
    auto engine = create_completion_engine(backend);
    const std::shared_ptr<completion_session_t> session = engine->create_session();

    auto first_sink = std::make_shared<recording_completion_sink_t>();
    auto second_sink = std::make_shared<recording_completion_sink_t>();

    completion_request_t first_request;
    first_request.prefix = "for";
    const std::shared_ptr<completion_operation_t> first_operation =
        session->request_completion(first_request, {}, first_sink);

    completion_request_t second_request;
    second_request.prefix = "while";
    const std::shared_ptr<completion_operation_t> second_operation =
        session->request_completion(second_request, {}, second_sink);

    QCOMPARE(backend->pending_requests.size(), std::size_t(2));
    QCOMPARE(first_operation->state(), completion_operation_state_t::CANCELLED);
    QCOMPARE(second_operation->is_terminal(), false);
    QCOMPARE(backend->pending_requests[0].operation->cancelled_times(), 1);
}

void tst_qompi_completion_session_t::stale_results_are_suppressed_after_newer_request()
{
    auto backend = std::make_shared<fake_completion_backend_t>();
    auto session = create_latest_wins_completion_session(backend);

    auto first_sink = std::make_shared<recording_completion_sink_t>();
    auto second_sink = std::make_shared<recording_completion_sink_t>();

    completion_request_t first_request;
    first_request.request_id = "old";
    first_request.prefix = "std::";
    const std::shared_ptr<completion_operation_t> first_operation =
        session->request_completion(first_request, {}, first_sink);

    completion_request_t second_request;
    second_request.request_id = "new";
    second_request.prefix = "QString";
    const std::shared_ptr<completion_operation_t> second_operation =
        session->request_completion(second_request, {}, second_sink);

    backend->emit_started(0);
    backend->emit_chunk(0, "vector");
    backend->emit_completed(0, "vector");

    QCOMPARE(first_sink->started_ids.empty(), true);
    QCOMPARE(first_sink->chunks.empty(), true);
    QCOMPARE(first_sink->completed_texts.empty(), true);
    QCOMPARE(first_operation->state(), completion_operation_state_t::CANCELLED);

    backend->emit_started(1);
    backend->emit_chunk(1, "Literal(");
    backend->emit_completed(1, "Literal(");

    QCOMPARE(second_sink->started_ids.size(), std::size_t(1));
    QCOMPARE(second_sink->chunks.size(), std::size_t(1));
    QCOMPARE(second_sink->completed_texts.size(), std::size_t(1));
    QCOMPARE(second_sink->completed_texts.front(), std::string("Literal("));
    QCOMPARE(second_operation->state(), completion_operation_state_t::COMPLETED);
}

}  // namespace qompi

QTEST_MAIN(qompi::tst_qompi_completion_session_t)

#include "tst_qompi_completion_session.moc"
