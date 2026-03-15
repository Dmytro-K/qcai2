#include "../src/context/ChatContextManager.h"
#include "../src/settings/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace qcai2;

class tst_ChatContext : public QObject
{
    Q_OBJECT

private slots:
    void storesArtifactsAndBuildsEnvelope();
    void newConversationIsIsolated();
};

void tst_ChatContext::storesArtifactsAndBuildsEnvelope()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QDir rootDir(tempDir.path());
    QVERIFY(rootDir.mkpath(QStringLiteral(".qcai2")));
    QVERIFY(rootDir.mkpath(QStringLiteral("docs")));

    QFile styleFile(rootDir.filePath(QStringLiteral("docs/CODING_STYLE.md")));
    QVERIFY(styleFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(styleFile.write("Always use braces.\n") >= 0);
    styleFile.close();

    ChatContextManager manager;
    QString error;
    QVERIFY(manager.setActiveWorkspace(QStringLiteral("workspace-1"), tempDir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    Settings settings;
    settings.modelName = QStringLiteral("gpt-5.4");
    settings.reasoningEffort = QStringLiteral("medium");
    settings.thinkingLevel = QStringLiteral("high");

    EditorContext::Snapshot snapshot;
    snapshot.projectDir = tempDir.path();
    snapshot.projectFilePath = rootDir.filePath(QStringLiteral("project.qtc"));
    snapshot.buildDir = rootDir.filePath(QStringLiteral("build"));
    snapshot.filePath = rootDir.filePath(QStringLiteral("src/main.cpp"));

    manager.syncWorkspaceState(snapshot, settings, &error);
    QVERIFY(error.isEmpty());

    const QString conversationId = manager.startNewConversation(QStringLiteral("Test"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(conversationId.isEmpty() == false, qPrintable(error));

    const QString runId = manager.beginRun(
        ContextRequestKind::AgentChat, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("medium"), QStringLiteral("high"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(runId.isEmpty() == false, qPrintable(error));

    for (int index = 0; index < 7; ++index)
    {
        QVERIFY(manager.appendUserMessage(
            runId,
            QStringLiteral("User message %1 with enough text to force summary refresh.")
                .arg(index),
            QStringLiteral("goal"), {}, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(manager.appendAssistantMessage(
            runId,
            QStringLiteral("Assistant response %1 with enough detail to keep the rolling summary "
                           "useful.")
                .arg(index),
            QStringLiteral("model_response"), {}, &error));
        QVERIFY(error.isEmpty());
    }

    QVERIFY(manager.appendArtifact(
        runId, QStringLiteral("build_log"), QStringLiteral("cmake build"),
        QStringLiteral("Build output line 1\nBuild output line 2\n"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.maybeRefreshSummary(&error));
    QVERIFY(error.isEmpty());

    const ContextEnvelope envelope = manager.buildContextEnvelope(
        ContextRequestKind::AgentChat, QStringLiteral("System prompt"),
        QStringList{QStringLiteral("Request-specific context")}, 4096, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(envelope.providerMessages.isEmpty() == false);
    QVERIFY(envelope.summary.isValid());

    bool hasMemoryBlock = false;
    bool hasSummaryBlock = false;
    bool hasArtifactBlock = false;
    for (const ChatMessage &message : envelope.providerMessages)
    {
        if (message.role != QStringLiteral("system"))
        {
            continue;
        }
        hasMemoryBlock =
            hasMemoryBlock || message.content.contains(QStringLiteral("Stable workspace memory:"));
        hasSummaryBlock =
            hasSummaryBlock || message.content.contains(QStringLiteral("Rolling summary"));
        hasArtifactBlock = hasArtifactBlock ||
                           message.content.contains(QStringLiteral("Relevant prior artifacts"));
    }

    QVERIFY(hasMemoryBlock);
    QVERIFY(hasSummaryBlock);
    QVERIFY(hasArtifactBlock);
    QVERIFY(QFileInfo::exists(manager.databasePath()));
    QVERIFY(QFileInfo(manager.databasePath()).isDir());
    QVERIFY(
        QFileInfo::exists(QDir(manager.databasePath()).filePath(QStringLiteral("format.json"))));
    QVERIFY(QFileInfo::exists(
        QDir(manager.databasePath())
            .filePath(QStringLiteral("conversations/%1/messages.jsonl").arg(conversationId))));

    const QDir artifactDir(manager.artifactDirectoryPath());
    QVERIFY(artifactDir.exists());
    QVERIFY(artifactDir.entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty() == false);
}

void tst_ChatContext::newConversationIsIsolated()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVERIFY(QDir(tempDir.path()).mkpath(QStringLiteral(".qcai2")));

    ChatContextManager manager;
    QString error;
    QVERIFY(manager.setActiveWorkspace(QStringLiteral("workspace-2"), tempDir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    const QString firstConversation = manager.startNewConversation({}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(firstConversation.isEmpty() == false, qPrintable(error));

    QString runId = manager.beginRun(ContextRequestKind::AgentChat, QStringLiteral("provider"),
                                     QStringLiteral("model"), QStringLiteral("off"),
                                     QStringLiteral("off"), true, {}, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(runId.isEmpty() == false);
    QVERIFY(manager.appendUserMessage(runId, QStringLiteral("first-conversation-marker"),
                                      QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.finishRun(runId, QStringLiteral("completed"), ProviderUsage{}, {}, &error));
    QVERIFY(error.isEmpty());

    const QString secondConversation = manager.startNewConversation({}, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(secondConversation.isEmpty() == false);
    QVERIFY(secondConversation != firstConversation);

    runId = manager.beginRun(ContextRequestKind::AgentChat, QStringLiteral("provider"),
                             QStringLiteral("model"), QStringLiteral("off"), QStringLiteral("off"),
                             true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(runId.isEmpty() == false, qPrintable(error));
    QVERIFY(manager.appendUserMessage(runId, QStringLiteral("second-conversation-marker"),
                                      QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());

    const ContextEnvelope envelope = manager.buildContextEnvelope(
        ContextRequestKind::AgentChat, QStringLiteral("System prompt"), {}, 1024, &error);
    QVERIFY(error.isEmpty());

    QString combinedPrompt;
    for (const ChatMessage &message : envelope.providerMessages)
    {
        combinedPrompt += message.content;
        combinedPrompt += QLatin1Char('\n');
    }

    QVERIFY(combinedPrompt.contains(QStringLiteral("second-conversation-marker")));
    QVERIFY(combinedPrompt.contains(QStringLiteral("first-conversation-marker")) == false);
}

QTEST_MAIN(tst_ChatContext)

#include "tst_chat_context.moc"
