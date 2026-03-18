#include "chat_context.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>

namespace qcai2
{

namespace
{

QJsonObject provider_usage_to_json(const provider_usage_t &usage)
{
    QJsonObject obj;
    if (usage.input_tokens >= 0)
    {
        obj[QStringLiteral("inputTokens")] = usage.input_tokens;
    }
    if (usage.output_tokens >= 0)
    {
        obj[QStringLiteral("outputTokens")] = usage.output_tokens;
    }
    if (usage.total_tokens >= 0)
    {
        obj[QStringLiteral("totalTokens")] = usage.total_tokens;
    }
    if (usage.reasoning_tokens >= 0)
    {
        obj[QStringLiteral("reasoningTokens")] = usage.reasoning_tokens;
    }
    if (usage.cached_input_tokens >= 0)
    {
        obj[QStringLiteral("cachedInputTokens")] = usage.cached_input_tokens;
    }
    return obj;
}

provider_usage_t provider_usage_from_json(const QJsonObject &obj)
{
    provider_usage_t usage;
    usage.input_tokens = obj.value(QStringLiteral("inputTokens")).toInt(-1);
    usage.output_tokens = obj.value(QStringLiteral("outputTokens")).toInt(-1);
    usage.total_tokens = obj.value(QStringLiteral("totalTokens")).toInt(-1);
    usage.reasoning_tokens = obj.value(QStringLiteral("reasoningTokens")).toInt(-1);
    usage.cached_input_tokens = obj.value(QStringLiteral("cachedInputTokens")).toInt(-1);
    return usage;
}

QJsonArray memory_items_to_json(const QList<memory_item_t> &items)
{
    QJsonArray array;
    for (const memory_item_t &item : items)
    {
        array.append(item.to_json());
    }
    return array;
}

QJsonArray messages_to_json(const QList<context_message_t> &items)
{
    QJsonArray array;
    for (const context_message_t &item : items)
    {
        array.append(item.to_json());
    }
    return array;
}

QJsonArray artifacts_to_json(const QList<artifact_record_t> &items)
{
    QJsonArray array;
    for (const artifact_record_t &item : items)
    {
        array.append(item.to_json());
    }
    return array;
}

QJsonArray provider_messages_to_json(const QList<chat_message_t> &items)
{
    QJsonArray array;
    for (const chat_message_t &item : items)
    {
        array.append(item.to_json());
    }
    return array;
}

}  // namespace

QJsonObject context_message_t::to_json() const
{
    return QJsonObject{{QStringLiteral("message_id"), message_id},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("conversation_id"), conversation_id},
                       {QStringLiteral("run_id"), run_id},
                       {QStringLiteral("sequence"), sequence},
                       {QStringLiteral("role"), role},
                       {QStringLiteral("source"), source},
                       {QStringLiteral("content"), content},
                       {QStringLiteral("createdAt"), created_at},
                       {QStringLiteral("token_estimate"), token_estimate},
                       {QStringLiteral("metadata"), metadata}};
}

context_message_t context_message_t::from_json(const QJsonObject &obj)
{
    context_message_t message;
    message.message_id = obj.value(QStringLiteral("message_id")).toString();
    message.workspace_id = obj.value(QStringLiteral("workspace_id")).toString();
    message.conversation_id = obj.value(QStringLiteral("conversation_id")).toString();
    message.run_id = obj.value(QStringLiteral("run_id")).toString();
    message.sequence = obj.value(QStringLiteral("sequence")).toInt();
    message.role = obj.value(QStringLiteral("role")).toString();
    message.source = obj.value(QStringLiteral("source")).toString();
    message.content = obj.value(QStringLiteral("content")).toString();
    message.created_at = obj.value(QStringLiteral("createdAt")).toString();
    message.token_estimate = obj.value(QStringLiteral("token_estimate")).toInt();
    message.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return message;
}

chat_message_t context_message_t::to_chat_message() const
{
    return {role, content};
}

bool context_summary_t::is_valid() const
{
    return !summary_id.isEmpty() && !conversation_id.isEmpty() && end_sequence >= start_sequence;
}

QJsonObject context_summary_t::to_json() const
{
    return QJsonObject{{QStringLiteral("summary_id"), summary_id},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("conversation_id"), conversation_id},
                       {QStringLiteral("version"), version},
                       {QStringLiteral("start_sequence"), start_sequence},
                       {QStringLiteral("end_sequence"), end_sequence},
                       {QStringLiteral("createdAt"), created_at},
                       {QStringLiteral("content"), content},
                       {QStringLiteral("token_estimate"), token_estimate},
                       {QStringLiteral("metadata"), metadata}};
}

context_summary_t context_summary_t::from_json(const QJsonObject &obj)
{
    context_summary_t summary;
    summary.summary_id = obj.value(QStringLiteral("summary_id")).toString();
    summary.workspace_id = obj.value(QStringLiteral("workspace_id")).toString();
    summary.conversation_id = obj.value(QStringLiteral("conversation_id")).toString();
    summary.version = obj.value(QStringLiteral("version")).toInt();
    summary.start_sequence = obj.value(QStringLiteral("start_sequence")).toInt();
    summary.end_sequence = obj.value(QStringLiteral("end_sequence")).toInt();
    summary.created_at = obj.value(QStringLiteral("createdAt")).toString();
    summary.content = obj.value(QStringLiteral("content")).toString();
    summary.token_estimate = obj.value(QStringLiteral("token_estimate")).toInt();
    summary.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return summary;
}

QJsonObject memory_item_t::to_json() const
{
    return QJsonObject{{QStringLiteral("memory_id"), memory_id},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("key"), key},
                       {QStringLiteral("category"), category},
                       {QStringLiteral("value"), value},
                       {QStringLiteral("source"), source},
                       {QStringLiteral("updatedAt"), updated_at},
                       {QStringLiteral("importance"), importance},
                       {QStringLiteral("token_estimate"), token_estimate},
                       {QStringLiteral("metadata"), metadata}};
}

memory_item_t memory_item_t::from_json(const QJsonObject &obj)
{
    memory_item_t item;
    item.memory_id = obj.value(QStringLiteral("memory_id")).toString();
    item.workspace_id = obj.value(QStringLiteral("workspace_id")).toString();
    item.key = obj.value(QStringLiteral("key")).toString();
    item.category = obj.value(QStringLiteral("category")).toString();
    item.value = obj.value(QStringLiteral("value")).toString();
    item.source = obj.value(QStringLiteral("source")).toString();
    item.updated_at = obj.value(QStringLiteral("updatedAt")).toString();
    item.importance = obj.value(QStringLiteral("importance")).toInt();
    item.token_estimate = obj.value(QStringLiteral("token_estimate")).toInt();
    item.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return item;
}

QJsonObject artifact_record_t::to_json() const
{
    return QJsonObject{{QStringLiteral("artifact_id"), artifact_id},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("conversation_id"), conversation_id},
                       {QStringLiteral("run_id"), run_id},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("storage_path"), storage_path},
                       {QStringLiteral("preview"), preview},
                       {QStringLiteral("createdAt"), created_at},
                       {QStringLiteral("token_estimate"), token_estimate},
                       {QStringLiteral("metadata"), metadata}};
}

artifact_record_t artifact_record_t::from_json(const QJsonObject &obj)
{
    artifact_record_t artifact;
    artifact.artifact_id = obj.value(QStringLiteral("artifact_id")).toString();
    artifact.workspace_id = obj.value(QStringLiteral("workspace_id")).toString();
    artifact.conversation_id = obj.value(QStringLiteral("conversation_id")).toString();
    artifact.run_id = obj.value(QStringLiteral("run_id")).toString();
    artifact.kind = obj.value(QStringLiteral("kind")).toString();
    artifact.title = obj.value(QStringLiteral("title")).toString();
    artifact.storage_path = obj.value(QStringLiteral("storage_path")).toString();
    artifact.preview = obj.value(QStringLiteral("preview")).toString();
    artifact.created_at = obj.value(QStringLiteral("createdAt")).toString();
    artifact.token_estimate = obj.value(QStringLiteral("token_estimate")).toInt();
    artifact.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return artifact;
}

bool conversation_record_t::is_valid() const
{
    return !conversation_id.isEmpty() && !workspace_id.isEmpty();
}

QJsonObject conversation_record_t::to_json() const
{
    return QJsonObject{{QStringLiteral("conversation_id"), conversation_id},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("workspaceRoot"), workspace_root},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("createdAt"), created_at},
                       {QStringLiteral("updatedAt"), updated_at},
                       {QStringLiteral("lastMessageSequence"), last_message_sequence},
                       {QStringLiteral("metadata"), metadata}};
}

conversation_record_t conversation_record_t::from_json(const QJsonObject &obj)
{
    conversation_record_t conversation;
    conversation.conversation_id = obj.value(QStringLiteral("conversation_id")).toString();
    conversation.workspace_id = obj.value(QStringLiteral("workspace_id")).toString();
    conversation.workspace_root = obj.value(QStringLiteral("workspaceRoot")).toString();
    conversation.title = obj.value(QStringLiteral("title")).toString();
    conversation.created_at = obj.value(QStringLiteral("createdAt")).toString();
    conversation.updated_at = obj.value(QStringLiteral("updatedAt")).toString();
    conversation.last_message_sequence = obj.value(QStringLiteral("lastMessageSequence")).toInt();
    conversation.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return conversation;
}

QJsonObject run_record_t::to_json() const
{
    return QJsonObject{{QStringLiteral("run_id"), run_id},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("conversation_id"), conversation_id},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("providerId"), provider_id},
                       {QStringLiteral("model"), model},
                       {QStringLiteral("reasoningEffort"), reasoning_effort},
                       {QStringLiteral("thinkingLevel"), thinking_level},
                       {QStringLiteral("dryRun"), dry_run},
                       {QStringLiteral("status"), status},
                       {QStringLiteral("startedAt"), started_at},
                       {QStringLiteral("finishedAt"), finished_at},
                       {QStringLiteral("usage"), provider_usage_to_json(usage)},
                       {QStringLiteral("metadata"), metadata}};
}

run_record_t run_record_t::from_json(const QJsonObject &obj)
{
    run_record_t run;
    run.run_id = obj.value(QStringLiteral("run_id")).toString();
    run.workspace_id = obj.value(QStringLiteral("workspace_id")).toString();
    run.conversation_id = obj.value(QStringLiteral("conversation_id")).toString();
    run.kind = obj.value(QStringLiteral("kind")).toString();
    run.provider_id = obj.value(QStringLiteral("providerId")).toString();
    run.model = obj.value(QStringLiteral("model")).toString();
    run.reasoning_effort = obj.value(QStringLiteral("reasoningEffort")).toString();
    run.thinking_level = obj.value(QStringLiteral("thinkingLevel")).toString();
    run.dry_run = obj.value(QStringLiteral("dryRun")).toBool(true);
    run.status = obj.value(QStringLiteral("status")).toString();
    run.started_at = obj.value(QStringLiteral("startedAt")).toString();
    run.finished_at = obj.value(QStringLiteral("finishedAt")).toString();
    run.usage = provider_usage_from_json(obj.value(QStringLiteral("usage")).toObject());
    run.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return run;
}

QJsonObject context_budget_t::to_json() const
{
    return QJsonObject{{QStringLiteral("totalTokens"), total_tokens},
                       {QStringLiteral("memoryTokens"), memory_tokens},
                       {QStringLiteral("summaryTokens"), summary_tokens},
                       {QStringLiteral("recentMessageTokens"), recent_message_tokens},
                       {QStringLiteral("artifactTokens"), artifact_tokens},
                       {QStringLiteral("maxRecentMessages"), max_recent_messages},
                       {QStringLiteral("maxArtifacts"), max_artifacts}};
}

QJsonObject context_envelope_t::to_json() const
{
    QJsonObject obj;
    obj[QStringLiteral("workspace_id")] = workspace_id;
    obj[QStringLiteral("conversation_id")] = conversation_id;
    obj[QStringLiteral("budget")] = budget.to_json();
    obj[QStringLiteral("memory_items")] = memory_items_to_json(memory_items);
    obj[QStringLiteral("summary")] = summary.to_json();
    obj[QStringLiteral("recent_messages")] = messages_to_json(recent_messages);
    obj[QStringLiteral("artifacts")] = artifacts_to_json(artifacts);
    obj[QStringLiteral("provider_messages")] = provider_messages_to_json(provider_messages);
    return obj;
}

int estimate_token_count(const QString &text)
{
    if (text.isEmpty() == true)
    {
        return 0;
    }
    const qsizetype estimated = (text.size() + 3) / 4;
    return qMax(1, static_cast<int>(estimated));
}

QString truncate_for_context(const QString &text, int max_chars)
{
    if (max_chars <= 0)
    {
        return {};
    }
    if (text.size() <= max_chars)
    {
        return text;
    }
    return text.left(max_chars) + QStringLiteral("\n… [truncated]");
}

QString new_context_id()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString now_utc_iso_string()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString request_kind_name(context_request_kind_t kind)
{
    switch (kind)
    {
        case context_request_kind_t::AGENT_CHAT:
            return QStringLiteral("agent");
        case context_request_kind_t::ASK:
            return QStringLiteral("ask");
        case context_request_kind_t::COMPLETION:
            return QStringLiteral("completion");
    }
    return QStringLiteral("agent");
}

context_budget_t budget_for_request(context_request_kind_t kind, int max_output_tokens)
{
    context_budget_t budget;
    switch (kind)
    {
        case context_request_kind_t::COMPLETION:
            budget.total_tokens = 320;
            budget.memory_tokens = 80;
            budget.summary_tokens = 120;
            budget.recent_message_tokens = 80;
            budget.artifact_tokens = 40;
            budget.max_recent_messages = 2;
            budget.max_artifacts = 1;
            break;
        case context_request_kind_t::ASK:
            budget.total_tokens = qBound(1200, max_output_tokens * 2, 8000);
            budget.memory_tokens = qMax(200, budget.total_tokens / 6);
            budget.summary_tokens = qMax(300, budget.total_tokens / 3);
            budget.recent_message_tokens = qMax(400, budget.total_tokens / 3);
            budget.artifact_tokens = qMax(150, budget.total_tokens / 8);
            budget.max_recent_messages = 10;
            budget.max_artifacts = 2;
            break;
        case context_request_kind_t::AGENT_CHAT:
            budget.total_tokens = qBound(2000, max_output_tokens * 3, 12000);
            budget.memory_tokens = qMax(300, budget.total_tokens / 6);
            budget.summary_tokens = qMax(500, budget.total_tokens / 3);
            budget.recent_message_tokens = qMax(700, budget.total_tokens / 3);
            budget.artifact_tokens = qMax(250, budget.total_tokens / 10);
            budget.max_recent_messages = 14;
            budget.max_artifacts = 3;
            break;
    }
    return budget;
}

QString default_artifact_file_suffix(const QString &kind)
{
    if (kind == QStringLiteral("diff"))
    {
        return QStringLiteral(".diff");
    }
    if (kind == QStringLiteral("build_log") || kind == QStringLiteral("test_result"))
    {
        return QStringLiteral(".log");
    }
    if (kind == QStringLiteral("json"))
    {
        return QStringLiteral(".json");
    }
    return QStringLiteral(".txt");
}

}  // namespace qcai2
