#include "../src/context/chat_context_manager.h"
#include "../src/settings/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace qcai2;

class tst_chat_context_t : public QObject
{
    Q_OBJECT

private slots:
    void stores_artifacts_and_builds_envelope();
    void new_conversation_is_isolated();
    void lists_conversations_with_most_recent_first();
    void renames_conversation();
    void deletes_conversation();
    void writes_hourly_usage_stats_csv();
    void excludes_superseded_soft_steer_messages_from_prompt_context();
    void compact_summary_replaces_pre_compacted_recent_messages();
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
    int memory_index = -1;
    int summary_index = -1;
    int artifact_index = -1;
    int request_context_index = -1;
    int system_message_index = 0;
    for (const chat_message_t &message : envelope.provider_messages)
    {
        if (message.role != QStringLiteral("system"))
        {
            continue;
        }
        if (message.content == QStringLiteral("System prompt"))
        {
            ++system_message_index;
            continue;
        }
        has_memory_block = has_memory_block ||
                           message.content.contains(QStringLiteral("Stable workspace memory:"));
        has_summary_block =
            has_summary_block || message.content.contains(QStringLiteral("Rolling summary"));
        has_artifact_block = has_artifact_block ||
                             message.content.contains(QStringLiteral("Relevant prior artifacts"));
        if (memory_index < 0 &&
            message.content.contains(QStringLiteral("Stable workspace memory:")))
        {
            memory_index = system_message_index;
        }
        if (summary_index < 0 && message.content.contains(QStringLiteral("Rolling summary")))
        {
            summary_index = system_message_index;
        }
        if (artifact_index < 0 &&
            message.content.contains(QStringLiteral("Relevant prior artifacts")))
        {
            artifact_index = system_message_index;
        }
        if (request_context_index < 0 &&
            message.content == QStringLiteral("Request-specific context"))
        {
            request_context_index = system_message_index;
        }
        ++system_message_index;
    }

    QVERIFY(has_memory_block);
    QVERIFY(has_summary_block);
    QVERIFY(has_artifact_block);
    QVERIFY(memory_index >= 0);
    QVERIFY(summary_index > memory_index);
    QVERIFY(artifact_index > summary_index);
    QVERIFY(request_context_index > artifact_index);
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

void tst_chat_context_t::lists_conversations_with_most_recent_first()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(
        manager.set_active_workspace(QStringLiteral("workspace-4"), temp_dir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    const QString first_conversation =
        manager.start_new_conversation(QStringLiteral("First"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(first_conversation.isEmpty() == false);

    const QString second_conversation =
        manager.start_new_conversation(QStringLiteral("Second"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(second_conversation.isEmpty() == false);

    QVERIFY(manager.set_active_workspace(QStringLiteral("workspace-4"), temp_dir.path(),
                                         first_conversation, &error));
    QVERIFY(error.isEmpty());

    const QString run_id = manager.begin_run(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("off"), QStringLiteral("off"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run_id.isEmpty() == false);
    QVERIFY(manager.append_user_message(run_id, QStringLiteral("refresh ordering"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QList<conversation_record_t> conversations = manager.conversations(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(conversations.size(), 2);
    QCOMPARE(conversations.constFirst().conversation_id, first_conversation);
    QCOMPARE(conversations.constLast().conversation_id, second_conversation);
}

void tst_chat_context_t::renames_conversation()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(
        manager.set_active_workspace(QStringLiteral("workspace-5"), temp_dir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    const QString conversation_id = manager.start_new_conversation({}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(conversation_id.isEmpty() == false);

    QVERIFY(manager.rename_conversation(conversation_id, QStringLiteral("Renamed Conversation"),
                                        &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QList<conversation_record_t> conversations = manager.conversations(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(conversations.size(), 1);
    QCOMPARE(conversations.constFirst().title, QStringLiteral("Renamed Conversation"));
    QCOMPARE(manager.active_conversation().title, QStringLiteral("Renamed Conversation"));
}

void tst_chat_context_t::deletes_conversation()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(
        manager.set_active_workspace(QStringLiteral("workspace-6"), temp_dir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    const QString first_conversation =
        manager.start_new_conversation(QStringLiteral("Delete Me"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(first_conversation.isEmpty() == false);

    const QString run_id = manager.begin_run(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("off"), QStringLiteral("off"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run_id.isEmpty() == false);
    QVERIFY(manager.append_user_message(run_id, QStringLiteral("message before delete"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(manager.append_artifact(run_id, QStringLiteral("plan"), QStringLiteral("Plan"),
                                    QStringLiteral("artifact content"), {}, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QString second_conversation =
        manager.start_new_conversation(QStringLiteral("Keep Me"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(second_conversation.isEmpty() == false);

    QVERIFY(manager.set_active_workspace(QStringLiteral("workspace-6"), temp_dir.path(),
                                         first_conversation, &error));
    QVERIFY(error.isEmpty());

    const QString conversation_dir =
        QDir(temp_dir.path())
            .filePath(
                QStringLiteral(".qcai2/chat-context/conversations/%1").arg(first_conversation));
    const QString runs_dir =
        QDir(temp_dir.path()).filePath(QStringLiteral(".qcai2/chat-context/runs"));
    const QString artifact_dir = manager.artifact_directory_path();
    QVERIFY(QFileInfo::exists(conversation_dir));
    QCOMPARE(QDir(runs_dir).entryList(QStringList() << "*.json", QDir::Files).size(), 1);
    QCOMPARE(QDir(artifact_dir).entryList(QDir::Files).size(), 1);

    QVERIFY(manager.delete_conversation(first_conversation, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QList<conversation_record_t> conversations = manager.conversations(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(conversations.size(), 1);
    QCOMPARE(conversations.constFirst().conversation_id, second_conversation);
    QVERIFY(manager.active_conversation_id().isEmpty());
    QVERIFY(QFileInfo::exists(conversation_dir) == false);
    QCOMPARE(QDir(runs_dir).entryList(QStringList() << "*.json", QDir::Files).size(), 0);
    QCOMPARE(QDir(artifact_dir).entryList(QDir::Files).size(), 0);
}

void tst_chat_context_t::writes_hourly_usage_stats_csv()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(
        manager.set_active_workspace(QStringLiteral("workspace-3"), temp_dir.path(), {}, &error));
    QVERIFY(error.isEmpty());

    const QString conversation_id =
        manager.start_new_conversation(QStringLiteral("Stats"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(conversation_id.isEmpty() == false);

    provider_usage_t first_usage;
    first_usage.input_tokens = 100;
    first_usage.output_tokens = 40;
    first_usage.total_tokens = 140;
    first_usage.reasoning_tokens = 10;
    first_usage.cached_input_tokens = 20;

    QString run_id =
        manager.begin_run(context_request_kind_t::AGENT_CHAT, QStringLiteral("provider-a"),
                          QStringLiteral("model-a"), QStringLiteral("medium"),
                          QStringLiteral("high"), false, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(manager.finish_run(run_id, QStringLiteral("completed"), first_usage, {}, &error));
    QVERIFY(error.isEmpty());

    provider_usage_t second_usage;
    second_usage.input_tokens = 50;
    second_usage.output_tokens = 25;
    second_usage.total_tokens = 75;
    second_usage.reasoning_tokens = 5;
    second_usage.cached_input_tokens = 10;

    run_id = manager.begin_run(context_request_kind_t::AGENT_CHAT, QStringLiteral("provider-a"),
                               QStringLiteral("model-a"), QStringLiteral("medium"),
                               QStringLiteral("high"), false, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(manager.finish_run(run_id, QStringLiteral("completed"), second_usage, {}, &error));
    QVERIFY(error.isEmpty());

    const QDir stats_dir(QDir(temp_dir.path()).filePath(QStringLiteral(".qcai2/stats")));
    QVERIFY(stats_dir.exists());

    const QStringList csv_files =
        stats_dir.entryList(QStringList{QStringLiteral("usage-*.csv")}, QDir::Files);
    QCOMPARE(csv_files.size(), 1);

    QFile stats_file(stats_dir.filePath(csv_files.constFirst()));
    QVERIFY(stats_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines =
        QString::fromUtf8(stats_file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);

    const QStringList header = lines.at(0).split(QLatin1Char(','));
    const QStringList row = lines.at(1).split(QLatin1Char(','));
    QCOMPARE(header.size(), 17);
    QCOMPARE(row.size(), 17);

    QCOMPARE(header.at(0), QStringLiteral("date_local"));
    QCOMPARE(header.at(1), QStringLiteral("hour_local"));
    QCOMPARE(header.at(2), QStringLiteral("tz"));
    QCOMPARE(header.at(3), QStringLiteral("provider"));
    QCOMPARE(header.at(4), QStringLiteral("model"));
    QCOMPARE(header.at(5), QStringLiteral("request_kind"));
    QCOMPARE(header.at(6), QStringLiteral("dry_run"));
    QCOMPARE(header.at(7), QStringLiteral("request_count"));
    QCOMPARE(header.at(8), QStringLiteral("completed_count"));
    QCOMPARE(header.at(9), QStringLiteral("non_completed_count"));
    QCOMPARE(header.at(10), QStringLiteral("usage_reported_count"));
    QCOMPARE(header.at(11), QStringLiteral("input_tokens"));
    QCOMPARE(header.at(12), QStringLiteral("output_tokens"));
    QCOMPARE(header.at(13), QStringLiteral("total_tokens"));
    QCOMPARE(header.at(14), QStringLiteral("reasoning_tokens"));
    QCOMPARE(header.at(15), QStringLiteral("cached_input_tokens"));
    QCOMPARE(header.at(16), QStringLiteral("duration_ms_total"));

    QVERIFY(row.at(0).isEmpty() == false);
    QVERIFY(row.at(1).isEmpty() == false);
    QVERIFY(QRegularExpression(QStringLiteral("^[+-]\\d{2}:\\d{2}$")).match(row.at(2)).hasMatch());
    QCOMPARE(row.at(3), QStringLiteral("provider-a"));
    QCOMPARE(row.at(4), QStringLiteral("model-a"));
    QCOMPARE(row.at(5), QStringLiteral("agent"));
    QCOMPARE(row.at(6), QStringLiteral("false"));
    QCOMPARE(row.at(7), QStringLiteral("2"));
    QCOMPARE(row.at(8), QStringLiteral("2"));
    QCOMPARE(row.at(9), QStringLiteral("0"));
    QCOMPARE(row.at(10), QStringLiteral("2"));
    QCOMPARE(row.at(11), QStringLiteral("150"));
    QCOMPARE(row.at(12), QStringLiteral("65"));
    QCOMPARE(row.at(13), QStringLiteral("215"));
    QCOMPARE(row.at(14), QStringLiteral("15"));
    QCOMPARE(row.at(15), QStringLiteral("30"));
    QVERIFY(row.at(16).toLongLong() >= 0);
}

void tst_chat_context_t::excludes_superseded_soft_steer_messages_from_prompt_context()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(manager.set_active_workspace(QStringLiteral("workspace-soft-steer"), temp_dir.path(),
                                         {}, &error));
    QVERIFY(error.isEmpty());

    const QString conversation_id =
        manager.start_new_conversation(QStringLiteral("Soft steer"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(conversation_id.isEmpty() == false);

    const QString run_id = manager.begin_run(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("medium"), QStringLiteral("medium"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run_id.isEmpty() == false);

    QVERIFY(manager.append_user_message(run_id, QStringLiteral("Initial request"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.append_assistant_message(
        run_id, QStringLiteral("Superseded draft"), QStringLiteral("model_response"),
        QJsonObject{{QStringLiteral("status"), QStringLiteral("superseded_by_user_steer")}},
        &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.append_user_message(run_id, QStringLiteral("Newest request"),
                                        QStringLiteral("soft_steer_followup"), {}, &error));
    QVERIFY(error.isEmpty());

    const context_envelope_t envelope = manager.build_context_envelope(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("System prompt"), {}, 1024, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QStringList assistant_messages;
    QStringList user_messages;
    for (const chat_message_t &message : envelope.provider_messages)
    {
        if (message.role == QStringLiteral("assistant"))
        {
            assistant_messages.append(message.content);
        }
        if (message.role == QStringLiteral("user"))
        {
            user_messages.append(message.content);
        }
    }

    QVERIFY(assistant_messages.contains(QStringLiteral("Superseded draft")) == false);
    QVERIFY(user_messages.contains(QStringLiteral("Initial request")));
    QVERIFY(user_messages.contains(QStringLiteral("Newest request")));
}

void tst_chat_context_t::compact_summary_replaces_pre_compacted_recent_messages()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir(temp_dir.path()).mkpath(QStringLiteral(".qcai2")));

    chat_context_manager_t manager;
    QString error;
    QVERIFY(manager.set_active_workspace(QStringLiteral("workspace-compaction"), temp_dir.path(),
                                         {}, &error));
    QVERIFY(error.isEmpty());

    const QString conversation_id =
        manager.start_new_conversation(QStringLiteral("Compaction"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(conversation_id.isEmpty() == false);

    QString run_id = manager.begin_run(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("medium"), QStringLiteral("medium"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run_id.isEmpty() == false);

    QVERIFY(manager.append_user_message(run_id, QStringLiteral("Old user message"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.append_assistant_message(run_id, QStringLiteral("Old assistant message"),
                                             QStringLiteral("model_response"), {}, &error));
    QVERIFY(error.isEmpty());

    QVERIFY(manager.append_compaction_summary(
        run_id, QStringLiteral("## Current Goal\nCompacted context\n"),
        QJsonObject{{QStringLiteral("source"), QStringLiteral("test")}}, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    run_id = manager.begin_run(context_request_kind_t::AGENT_CHAT, QStringLiteral("provider"),
                               QStringLiteral("model"), QStringLiteral("medium"),
                               QStringLiteral("medium"), true, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run_id.isEmpty() == false);

    QVERIFY(manager.append_user_message(run_id, QStringLiteral("New user message"),
                                        QStringLiteral("goal"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(manager.append_assistant_message(run_id, QStringLiteral("New assistant message"),
                                             QStringLiteral("model_response"), {}, &error));
    QVERIFY(error.isEmpty());

    const context_envelope_t envelope = manager.build_context_envelope(
        context_request_kind_t::AGENT_CHAT, QStringLiteral("System prompt"), {}, 2048, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(envelope.summary.is_valid());
    QVERIFY(envelope.summary.content.contains(QStringLiteral("Compacted context")));

    QStringList user_messages;
    QStringList assistant_messages;
    for (const chat_message_t &message : envelope.provider_messages)
    {
        if (message.role == QStringLiteral("user"))
        {
            user_messages.append(message.content);
        }
        if (message.role == QStringLiteral("assistant"))
        {
            assistant_messages.append(message.content);
        }
    }

    QVERIFY(user_messages.contains(QStringLiteral("Old user message")) == false);
    QVERIFY(assistant_messages.contains(QStringLiteral("Old assistant message")) == false);
    QVERIFY(user_messages.contains(QStringLiteral("New user message")));
    QVERIFY(assistant_messages.contains(QStringLiteral("New assistant message")));
}

QTEST_MAIN(tst_chat_context_t)

#include "tst_chat_context.moc"
