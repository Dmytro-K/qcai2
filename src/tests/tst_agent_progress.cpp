/*!
    @file
    @brief Tests provider-agnostic agent progress mapping, classification, and rendering.
*/

#include "../src/progress/AgentProgress.h"
#include "../src/progress/AgentProgressTracker.h"
#include "../src/progress/AgentStatusFormatter.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace qcai2;

class tst_agent_progress_t : public QObject
{
    Q_OBJECT

private slots:
    void classifies_tool_names();
    void maps_provider_events_to_thinking_and_tools();
    void coalesces_noisy_thinking_updates();
    void renders_final_lifecycle();
    void renders_structured_logs_in_non_interactive_mode();
    void renders_errors();
};

void tst_agent_progress_t::classifies_tool_names()
{
    QCOMPARE(classify_tool_operation(QStringLiteral("compact")),
             agent_progress_operation_t::EXPLORE);
    QCOMPARE(
        progress_label_for_tool(QStringLiteral("compact"), agent_progress_operation_t::EXPLORE),
        QStringLiteral("compact command"));

    QCOMPARE(classify_tool_operation(QStringLiteral("run_tests")),
             agent_progress_operation_t::TEST);
    QCOMPARE(classify_tool_operation(QStringLiteral("run_build")),
             agent_progress_operation_t::BUILD);
    QCOMPARE(classify_tool_operation(QStringLiteral("search_repo")),
             agent_progress_operation_t::SEARCH);
    QCOMPARE(
        progress_label_for_tool(QStringLiteral("search_repo"), agent_progress_operation_t::SEARCH),
        QStringLiteral("repository"));

    QCOMPARE(classify_tool_operation(QStringLiteral("apply_patch")),
             agent_progress_operation_t::APPLY_CHANGES);
    QCOMPARE(classify_tool_operation(QStringLiteral("read_file")),
             agent_progress_operation_t::READ);
    QCOMPARE(
        progress_label_for_tool(QStringLiteral("read_file"), agent_progress_operation_t::READ),
        QStringLiteral("file"));
}

void tst_agent_progress_t::maps_provider_events_to_thinking_and_tools()
{
    agent_progress_tracker_t tracker(QStringLiteral("copilot"),
                                     agent_status_render_mode_t::INTERACTIVE, false);

    const agent_progress_render_result_t request_result =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::REQUEST_STARTED,
                                           QStringLiteral("copilot"),
                                           QStringLiteral("request.started"),
                                           {},
                                           {}});
    QVERIFY(request_result.status_changed);
    QCOMPARE(request_result.status_text, QStringLiteral("Thinking"));

    const agent_progress_render_result_t tool_result =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::TOOL_STARTED,
                                           QStringLiteral("copilot"),
                                           QStringLiteral("tool.execution_start"),
                                           QStringLiteral("compact"),
                                           {}});
    QVERIFY(tool_result.status_changed);
    QCOMPARE(tool_result.status_text, QStringLiteral("Exploring compact command"));

    const agent_progress_render_result_t completed_tool_result =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::TOOL_COMPLETED,
                                           QStringLiteral("copilot"),
                                           QStringLiteral("tool.execution_complete"),
                                           QStringLiteral("compact"),
                                           {}});
    QVERIFY(!completed_tool_result.status_changed);
}

void tst_agent_progress_t::coalesces_noisy_thinking_updates()
{
    agent_progress_tracker_t tracker(QStringLiteral("openai"),
                                     agent_status_render_mode_t::INTERACTIVE, false);

    const agent_progress_render_result_t started =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::REQUEST_STARTED,
                                           QStringLiteral("openai"),
                                           QStringLiteral("request.started"),
                                           {},
                                           {}});
    QVERIFY(started.status_changed);
    QCOMPARE(started.status_text, QStringLiteral("Thinking"));

    const agent_progress_render_result_t first_reasoning =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::REASONING_DELTA,
                                           QStringLiteral("openai"),
                                           QStringLiteral("response.reasoning.delta"),
                                           {},
                                           QStringLiteral("step 1")});
    QVERIFY(!first_reasoning.status_changed);

    const agent_progress_render_result_t first_message =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::MESSAGE_DELTA,
                                           QStringLiteral("openai"),
                                           QStringLiteral("assistant.message_delta"),
                                           {},
                                           QStringLiteral("hello")});
    QVERIFY(!first_message.status_changed);

    const agent_progress_render_result_t second_message =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::MESSAGE_DELTA,
                                           QStringLiteral("openai"),
                                           QStringLiteral("assistant.message_delta"),
                                           {},
                                           QStringLiteral("world")});
    QVERIFY(!second_message.status_changed);
}

void tst_agent_progress_t::renders_final_lifecycle()
{
    agent_progress_tracker_t tracker(QStringLiteral("openai"),
                                     agent_status_render_mode_t::INTERACTIVE, false);

    const agent_progress_render_result_t final_started =
        tracker.handle_normalized_event({agent_progress_event_kind_t::FINAL_ANSWER_STARTED,
                                         agent_progress_operation_t::NONE,
                                         QStringLiteral("openai"),
                                         QStringLiteral("controller.final.start"),
                                         {},
                                         {},
                                         {}});
    QVERIFY(final_started.status_changed);
    QCOMPARE(final_started.status_text, QStringLiteral("Preparing final answer"));

    const agent_progress_render_result_t final_completed =
        tracker.handle_normalized_event({agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED,
                                         agent_progress_operation_t::NONE,
                                         QStringLiteral("openai"),
                                         QStringLiteral("controller.final.completed"),
                                         {},
                                         {},
                                         {}});
    QVERIFY(final_completed.status_changed);
    QCOMPARE(final_completed.status_text, QStringLiteral("Done"));
}

void tst_agent_progress_t::renders_structured_logs_in_non_interactive_mode()
{
    agent_progress_tracker_t tracker(QStringLiteral("copilot"),
                                     agent_status_render_mode_t::NON_INTERACTIVE, false);

    const agent_progress_render_result_t result =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::TOOL_STARTED,
                                           QStringLiteral("copilot"),
                                           QStringLiteral("tool.execution_start"),
                                           QStringLiteral("search_repo"),
                                           {}});
    QVERIFY(result.status_changed);
    QCOMPARE(result.status_text, QStringLiteral("Searching repository"));
    QVERIFY(result.stable_log_line.isEmpty() == false);

    const QJsonDocument doc = QJsonDocument::fromJson(result.stable_log_line.toUtf8());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("type")).toString(), QStringLiteral("agent_status"));
    QCOMPARE(root.value(QStringLiteral("phase")).toString(), QStringLiteral("searching"));
    QCOMPARE(root.value(QStringLiteral("text")).toString(),
             QStringLiteral("Searching repository"));
    QCOMPARE(root.value(QStringLiteral("subject")).toString(), QStringLiteral("repository"));
}

void tst_agent_progress_t::renders_errors()
{
    agent_progress_tracker_t tracker(QStringLiteral("openai"),
                                     agent_status_render_mode_t::INTERACTIVE, false);

    const agent_progress_render_result_t result =
        tracker.handle_provider_raw_event({provider_raw_event_kind_t::ERROR_EVENT,
                                           QStringLiteral("openai"),
                                           QStringLiteral("response.error"),
                                           {},
                                           QStringLiteral("network failed")});
    QVERIFY(result.status_changed);
    QCOMPARE(result.status_text, QStringLiteral("Error: network failed"));
}

QTEST_GUILESS_MAIN(tst_agent_progress_t)

#include "tst_agent_progress.moc"
