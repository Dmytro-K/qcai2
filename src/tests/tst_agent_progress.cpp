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

class tst_AgentProgress : public QObject
{
    Q_OBJECT

private slots:
    void classifiesToolNames();
    void mapsProviderEventsToThinkingAndTools();
    void coalescesNoisyThinkingUpdates();
    void rendersFinalLifecycle();
    void rendersStructuredLogsInNonInteractiveMode();
    void rendersErrors();
};

void tst_AgentProgress::classifiesToolNames()
{
    QCOMPARE(classifyToolOperation(QStringLiteral("compact")), AgentProgressOperation::Explore);
    QCOMPARE(progressLabelForTool(QStringLiteral("compact"), AgentProgressOperation::Explore),
             QStringLiteral("compact command"));

    QCOMPARE(classifyToolOperation(QStringLiteral("run_tests")), AgentProgressOperation::Test);
    QCOMPARE(classifyToolOperation(QStringLiteral("run_build")), AgentProgressOperation::Build);
    QCOMPARE(classifyToolOperation(QStringLiteral("search_repo")), AgentProgressOperation::Search);
    QCOMPARE(progressLabelForTool(QStringLiteral("search_repo"), AgentProgressOperation::Search),
             QStringLiteral("repository"));

    QCOMPARE(classifyToolOperation(QStringLiteral("apply_patch")),
             AgentProgressOperation::ApplyChanges);
    QCOMPARE(classifyToolOperation(QStringLiteral("read_file")), AgentProgressOperation::Read);
    QCOMPARE(progressLabelForTool(QStringLiteral("read_file"), AgentProgressOperation::Read),
             QStringLiteral("file"));
}

void tst_AgentProgress::mapsProviderEventsToThinkingAndTools()
{
    AgentProgressTracker tracker(QStringLiteral("copilot"), AgentStatusRenderMode::Interactive,
                                 false);

    const AgentProgressRenderResult requestResult =
        tracker.handleProviderRawEvent({ProviderRawEventKind::RequestStarted,
                                        QStringLiteral("copilot"),
                                        QStringLiteral("request.started"),
                                        {},
                                        {}});
    QVERIFY(requestResult.statusChanged);
    QCOMPARE(requestResult.statusText, QStringLiteral("Thinking"));

    const AgentProgressRenderResult toolResult =
        tracker.handleProviderRawEvent({ProviderRawEventKind::ToolStarted,
                                        QStringLiteral("copilot"),
                                        QStringLiteral("tool.execution_start"),
                                        QStringLiteral("compact"),
                                        {}});
    QVERIFY(toolResult.statusChanged);
    QCOMPARE(toolResult.statusText, QStringLiteral("Exploring compact command"));

    const AgentProgressRenderResult completedToolResult =
        tracker.handleProviderRawEvent({ProviderRawEventKind::ToolCompleted,
                                        QStringLiteral("copilot"),
                                        QStringLiteral("tool.execution_complete"),
                                        QStringLiteral("compact"),
                                        {}});
    QVERIFY(!completedToolResult.statusChanged);
}

void tst_AgentProgress::coalescesNoisyThinkingUpdates()
{
    AgentProgressTracker tracker(QStringLiteral("openai"), AgentStatusRenderMode::Interactive,
                                 false);

    const AgentProgressRenderResult started =
        tracker.handleProviderRawEvent({ProviderRawEventKind::RequestStarted,
                                        QStringLiteral("openai"),
                                        QStringLiteral("request.started"),
                                        {},
                                        {}});
    QVERIFY(started.statusChanged);
    QCOMPARE(started.statusText, QStringLiteral("Thinking"));

    const AgentProgressRenderResult firstReasoning =
        tracker.handleProviderRawEvent({ProviderRawEventKind::ReasoningDelta,
                                        QStringLiteral("openai"),
                                        QStringLiteral("response.reasoning.delta"),
                                        {},
                                        QStringLiteral("step 1")});
    QVERIFY(!firstReasoning.statusChanged);

    const AgentProgressRenderResult firstMessage =
        tracker.handleProviderRawEvent({ProviderRawEventKind::MessageDelta,
                                        QStringLiteral("openai"),
                                        QStringLiteral("assistant.message_delta"),
                                        {},
                                        QStringLiteral("hello")});
    QVERIFY(!firstMessage.statusChanged);

    const AgentProgressRenderResult secondMessage =
        tracker.handleProviderRawEvent({ProviderRawEventKind::MessageDelta,
                                        QStringLiteral("openai"),
                                        QStringLiteral("assistant.message_delta"),
                                        {},
                                        QStringLiteral("world")});
    QVERIFY(!secondMessage.statusChanged);
}

void tst_AgentProgress::rendersFinalLifecycle()
{
    AgentProgressTracker tracker(QStringLiteral("openai"), AgentStatusRenderMode::Interactive,
                                 false);

    const AgentProgressRenderResult finalStarted =
        tracker.handleNormalizedEvent({AgentProgressEventKind::FinalAnswerStarted,
                                       AgentProgressOperation::None,
                                       QStringLiteral("openai"),
                                       QStringLiteral("controller.final.start"),
                                       {},
                                       {},
                                       {}});
    QVERIFY(finalStarted.statusChanged);
    QCOMPARE(finalStarted.statusText, QStringLiteral("Preparing final answer"));

    const AgentProgressRenderResult finalCompleted =
        tracker.handleNormalizedEvent({AgentProgressEventKind::FinalAnswerCompleted,
                                       AgentProgressOperation::None,
                                       QStringLiteral("openai"),
                                       QStringLiteral("controller.final.completed"),
                                       {},
                                       {},
                                       {}});
    QVERIFY(finalCompleted.statusChanged);
    QCOMPARE(finalCompleted.statusText, QStringLiteral("Done"));
}

void tst_AgentProgress::rendersStructuredLogsInNonInteractiveMode()
{
    AgentProgressTracker tracker(QStringLiteral("copilot"), AgentStatusRenderMode::NonInteractive,
                                 false);

    const AgentProgressRenderResult result =
        tracker.handleProviderRawEvent({ProviderRawEventKind::ToolStarted,
                                        QStringLiteral("copilot"),
                                        QStringLiteral("tool.execution_start"),
                                        QStringLiteral("search_repo"),
                                        {}});
    QVERIFY(result.statusChanged);
    QCOMPARE(result.statusText, QStringLiteral("Searching repository"));
    QVERIFY(result.stableLogLine.isEmpty() == false);

    const QJsonDocument doc = QJsonDocument::fromJson(result.stableLogLine.toUtf8());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("type")).toString(), QStringLiteral("agent_status"));
    QCOMPARE(root.value(QStringLiteral("phase")).toString(), QStringLiteral("searching"));
    QCOMPARE(root.value(QStringLiteral("text")).toString(),
             QStringLiteral("Searching repository"));
    QCOMPARE(root.value(QStringLiteral("subject")).toString(), QStringLiteral("repository"));
}

void tst_AgentProgress::rendersErrors()
{
    AgentProgressTracker tracker(QStringLiteral("openai"), AgentStatusRenderMode::Interactive,
                                 false);

    const AgentProgressRenderResult result =
        tracker.handleProviderRawEvent({ProviderRawEventKind::Error,
                                        QStringLiteral("openai"),
                                        QStringLiteral("response.error"),
                                        {},
                                        QStringLiteral("network failed")});
    QVERIFY(result.statusChanged);
    QCOMPARE(result.statusText, QStringLiteral("Error: network failed"));
}

QTEST_GUILESS_MAIN(tst_AgentProgress)

#include "tst_agent_progress.moc"
