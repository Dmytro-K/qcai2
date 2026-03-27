/*! @file
    @brief Regression tests for the qompi Qt facade.
*/

#include <qompi/qt_completion_session.h>

#include <QSignalSpy>
#include <QtTest>

#include <memory>
#include <utility>

namespace qompi
{

namespace
{

class fake_operation_t final : public completion_operation_t
{
public:
    explicit fake_operation_t(std::string request_id) : request_id_value(std::move(request_id))
    {
    }

    std::string request_id() const override
    {
        return this->request_id_value;
    }

    completion_operation_state_t state() const override
    {
        return this->current_state;
    }

    void cancel() override
    {
        this->current_state = completion_operation_state_t::CANCELLED;
    }

private:
    std::string request_id_value;
    completion_operation_state_t current_state = completion_operation_state_t::QUEUED;
};

class fake_session_t final : public completion_session_t
{
public:
    std::shared_ptr<completion_operation_t>
    request_completion(const completion_request_t &request, const completion_options_t &options,
                       std::shared_ptr<completion_sink_t> sink) override
    {
        (void)options;
        this->last_request = request;
        this->last_sink = std::move(sink);
        this->last_operation = std::make_shared<fake_operation_t>(request.request_id);
        return this->last_operation;
    }

    void cancel_active() override
    {
        if (this->last_operation != nullptr)
        {
            this->last_operation->cancel();
        }
    }

    completion_request_t last_request;
    std::shared_ptr<completion_sink_t> last_sink;
    std::shared_ptr<fake_operation_t> last_operation;
};

}  // namespace

class tst_qompi_qt_completion_session_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * The Qt facade should forward core sink callbacks as queued Qt signals.
     */
    void forwards_completion_callbacks_as_signals();
};

void tst_qompi_qt_completion_session_t::forwards_completion_callbacks_as_signals()
{
    auto session = std::make_shared<fake_session_t>();
    qt_completion_session_t qt_session(session);

    QSignalSpy started_spy(&qt_session, &qt_completion_session_t::started);
    QSignalSpy chunk_spy(&qt_session, &qt_completion_session_t::chunk_ready);
    QSignalSpy completed_spy(&qt_session, &qt_completion_session_t::completed);

    completion_request_t request;
    request.request_id = "qt-request";
    request.prefix = "QStringLiteral";
    qt_session.request_completion(request);

    QVERIFY(session->last_sink != nullptr);

    session->last_sink->on_started("qt-request");
    completion_chunk_t chunk;
    chunk.text_delta = "(\"value\")";
    chunk.stable_text = "(\"value\")";
    session->last_sink->on_chunk(chunk);

    completion_result_t result;
    result.request_id = "qt-request";
    result.text = "(\"value\")";
    result.finish_reason = "stop";
    session->last_sink->on_completed(result);

    QTRY_COMPARE(started_spy.count(), 1);
    QTRY_COMPARE(chunk_spy.count(), 1);
    QTRY_COMPARE(completed_spy.count(), 1);

    QCOMPARE(started_spy.at(0).at(0).toString(), QStringLiteral("qt-request"));
    QCOMPARE(chunk_spy.at(0).at(0).toString(), QStringLiteral("(\"value\")"));
    QCOMPARE(completed_spy.at(0).at(1).toString(), QStringLiteral("(\"value\")"));
}

}  // namespace qompi

QTEST_MAIN(qompi::tst_qompi_qt_completion_session_t)

#include "tst_qompi_qt_completion_session.moc"
