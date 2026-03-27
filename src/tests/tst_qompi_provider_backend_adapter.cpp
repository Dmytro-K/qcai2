/*! @file
    @brief Regression tests for the qcai2-to-qompi provider backend adapter.
*/

#include "../src/completion/qompi_provider_backend_adapter.h"

#include <qompi/completion_session.h>
#include <qompi/completion_sink.h>

#include <QtTest>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace qcai2
{

namespace
{

class fake_provider_t final : public iai_provider_t
{
public:
    QString id() const override
    {
        return QStringLiteral("ollama");
    }

    QString display_name() const override
    {
        return QStringLiteral("Fake Ollama");
    }

    void complete(const QList<chat_message_t> &messages, const QString &model,
                  double /*temperature*/, int /*max_tokens*/, const QString &reasoning_effort,
                  completion_callback_t callback, stream_callback_t stream_callback,
                  progress_callback_t progress_callback) override
    {
        this->messages = messages;
        this->model = model;
        this->reasoning_effort = reasoning_effort;
        this->completion_callback = std::move(callback);
        this->stream_callback = std::move(stream_callback);
        this->progress_callback = std::move(progress_callback);
    }

    void cancel() override
    {
    }

    void set_base_url(const QString &url) override
    {
        this->base_url = url;
    }

    void set_api_key(const QString &key) override
    {
        this->api_key = key;
    }

    QList<chat_message_t> messages;
    QString model;
    QString reasoning_effort;
    QString base_url;
    QString api_key;
    completion_callback_t completion_callback;
    stream_callback_t stream_callback;
    progress_callback_t progress_callback;
};

class recording_completion_sink_t final : public qompi::completion_sink_t
{
public:
    void on_started(const std::string &request_id) override
    {
        this->started_ids.push_back(request_id);
    }

    void on_debug_event(const std::string &stage, const std::string &message) override
    {
        this->debug_events.push_back(QString::fromStdString(stage + ":" + message));
    }

    void on_chunk(const qompi::completion_chunk_t &chunk) override
    {
        this->chunks.push_back(chunk.stable_text);
    }

    void on_completed(const qompi::completion_result_t &result) override
    {
        this->completed_results.push_back(result);
    }

    void on_failed(const qompi::completion_error_t &error) override
    {
        this->failed_messages.push_back(QString::fromStdString(error.message));
    }

    std::vector<std::string> started_ids;
    std::vector<QString> debug_events;
    std::vector<std::string> chunks;
    std::vector<qompi::completion_result_t> completed_results;
    std::vector<QString> failed_messages;
};

}  // namespace

class tst_qompi_provider_backend_adapter_t : public QObject
{
    Q_OBJECT

private slots:
    void empty_final_callback_falls_back_to_streamed_message_deltas();
};

void tst_qompi_provider_backend_adapter_t::
    empty_final_callback_falls_back_to_streamed_message_deltas()
{
    fake_provider_t provider;
    const std::shared_ptr<qompi::completion_session_t> session =
        qompi::create_latest_wins_completion_session(create_qompi_provider_backend(&provider));
    const auto sink = std::make_shared<recording_completion_sink_t>();

    qompi::completion_request_t request;
    request.request_id = "request-1";
    request.prefix = "    for";
    request.suffix = "\n";
    request.user_prompt = "Complete the loop.";
    request.system_context_blocks = {"system context"};

    qompi::completion_options_t options;
    options.model = "starcoder:1b";

    const std::shared_ptr<qompi::completion_operation_t> operation =
        session->request_completion(request, options, sink);

    QVERIFY(operation != nullptr);
    QVERIFY(provider.completion_callback != nullptr);
    QVERIFY(provider.stream_callback != nullptr);
    QVERIFY(provider.progress_callback != nullptr);
    QCOMPARE(provider.messages.size(), 2);
    QCOMPARE(provider.messages.at(0).role, QStringLiteral("system"));
    QCOMPARE(provider.messages.at(1).role, QStringLiteral("user"));

    provider.progress_callback({provider_raw_event_kind_t::MESSAGE_DELTA,
                                provider.id(),
                                QStringLiteral("assistant.message_delta"),
                                {},
                                QStringLiteral("for (const auto &message : messages)")});
    provider.completion_callback(QString(), QString(), {});

    QCOMPARE(sink->failed_messages.size(), std::size_t(0));
    QCOMPARE(sink->started_ids.size(), std::size_t(1));
    QCOMPARE(sink->completed_results.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(sink->completed_results.front().text),
             QStringLiteral(" (const auto &message : messages)"));
    QVERIFY(std::any_of(sink->debug_events.begin(), sink->debug_events.end(),
                        [](const QString &event) {
                            return event.contains(QStringLiteral("provider.callback_received:")) &&
                                   event.contains(QStringLiteral("used_stream_fallback=true"));
                        }));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_qompi_provider_backend_adapter_t)

#include "tst_qompi_provider_backend_adapter.moc"
