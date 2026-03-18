#include "../src/context/chat_context_manager.h"
#include "../src/settings/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace qcai2;

class tst_chat_context_t : public QObject
{
    Q_OBJECT

private slots:
    void stores_artifacts_and_builds_envelope();
    void new_conversation_is_isolated();
};

void tst_chat_context_t::stores_artifacts_and_builds_envelope()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QDir root_dir(temp_dir.path());
    QVERIFY(root_dir.mkpath(QStringLiteral(".qcai2")));
    QVERIFY(root_dir.mkpath(QStringLiteral("docs")));

    QFile style_file(root_dir.filePath(QStringLiteral("docs/CODING_STYLE.md")));
    QVERIFY(style_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(style_file.write("Always use braces.\n") >= 0);
    style_file.close();

    chat_context_manager_t manager;
    QString error;
    QVERIFY(
        manager.set_active_workspace(QStringLiteral("workspace-1"), temp_dir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    settings_t settings;
    settings.model_name = QStringLiteral("gpt-5.4");
    settings.reasoning_effort = QStringLiteral("medium");
    settings.thinking_level = QStringLiteral("high");

    editor_context_t::snapshot_t snapshot;
    snapshot.project_dir = temp_dir.path();
    snapshot.project_file_path = root_dir.filePath(QStringLiteral("project.qtc"));
    snapshot.build_dir = root_dir.filePath(QStringLiteral("build"));
    snapshot.file_path = root_dir.filePath(QStringLiteral("src/main.cpp"));

    manager.sync_workspace_state(snapshot, settings, &error);
    QVERIFY(error.isEmpty());

    const QString conversation_id = manager.start_new_conversation(QStringLiteral("Test"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(conversation_id.isEmpty() == false, qPrintable(error));

    const QString run_id = manager.begin_run(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("medium"), QStringLiteral("high"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(run_id.isEmpty() == false, qPrintable(error));

    for (int index = 0; index < 7; ++index)
    {
        QVERIFY(manager.append_user_message(
            run_id,
            QStringLiteral("User message %1 with enough text to force summary refresh.")
                .arg(index),
            QStringLiteral("goal"), {}, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(manager.append_assistant_message(
            run_id,
            QStringLiteral("Assistant response %1 with enough detail to keep the rolling summary "
                           "useful.")
                .arg(index),
            QStringLiteral("model_response"), {}, &error));
        QVERIFY(error.isEmpty());
    }

    QVERIFY(manager.append_artifact(
        run_id, QStringLiteral("build_log"), QStringLiteral("cmake build"),
        QStringLiteral("Build output line 1\nBuild output line 2\n"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.maybe_refresh_summary(&error));
    QVERIFY(error.isEmpty());

    const context_envelope_t envelope = manager.build_context_envelope(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("System prompt"),
        QStringList{QStringLiteral("Request-specific context")}, 4096, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(envelope.provider_messages.isEmpty() == false);
    QVERIFY(envelope.summary.is_valid());

    bool has_memory_block = false;
    bool has_summary_block = false;
    bool has_artifact_block = false;
    for (const chat_message_t &message : envelope.provider_messages)
    {
        if (message.role != QStringLiteral("system"))
        {
            continue;
        }
        has_memory_block = has_memory_block ||
                           message.content.contains(QStringLiteral("Stable workspace memory:"));
        has_summary_block =
            has_summary_block || message.content.contains(QStringLiteral("Rolling summary"));
        has_artifact_block = has_artifact_block ||
                             message.content.contains(QStringLiteral("Relevant prior artifacts"));
    }

    QVERIFY(has_memory_block);
    QVERIFY(has_summary_block);
    QVERIFY(has_artifact_block);
    QVERIFY(QFileInfo::exists(manager.database_path()));
    QVERIFY(QFileInfo(manager.database_path()).isDir());
    QVERIFY(
        QFileInfo::exists(QDir(manager.database_path()).filePath(QStringLiteral("format.json"))));
    QVERIFY(QFileInfo::exists(
        QDir(manager.database_path())
            .filePath(QStringLiteral("conversations/%1/messages.jsonl").arg(conversation_id))));

    const QDir artifact_dir(manager.artifact_directory_path());
    QVERIFY(artifact_dir.exists());
    QVERIFY(artifact_dir.entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty() == false);
}

void tst_chat_context_t::new_conversation_is_isolated()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(
        manager.set_active_workspace(QStringLiteral("workspace-2"), temp_dir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    const QString first_conversation = manager.start_new_conversation({}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(first_conversation.isEmpty() == false, qPrintable(error));

    QString run_id = manager.begin_run(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("off"), QStringLiteral("off"), true, {}, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(run_id.isEmpty() == false);
    QVERIFY(manager.append_user_message(run_id, QStringLiteral("first-conversation-marker"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(
        manager.finish_run(run_id, QStringLiteral("completed"), provider_usage_t{}, {}, &error));
    QVERIFY(error.isEmpty());

    const QString second_conversation = manager.start_new_conversation({}, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(second_conversation.isEmpty() == false);
    QVERIFY(second_conversation != first_conversation);

    run_id = manager.begin_run(context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"),
                               QStringLiteral("model"), QStringLiteral("off"),
                               QStringLiteral("off"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(run_id.isEmpty() == false, qPrintable(error));
    QVERIFY(manager.append_user_message(run_id, QStringLiteral("second-conversation-marker"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());

    const context_envelope_t envelope = manager.build_context_envelope(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("System prompt"), {}, 1024, &error);
    QVERIFY(error.isEmpty());

    QString combined_prompt;
    for (const chat_message_t &message : envelope.provider_messages)
    {
        combined_prompt += message.content;
        combined_prompt += QLatin1Char('\n');
    }

    QVERIFY(combined_prompt.contains(QStringLiteral("second-conversation-marker")));
    QVERIFY(combined_prompt.contains(QStringLiteral("first-conversation-marker")) == false);
}

QTEST_MAIN(tst_chat_context_t)

#include "tst_chat_context.moc"
