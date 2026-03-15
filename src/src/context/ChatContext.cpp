#include "ChatContext.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>

namespace qcai2
{

namespace
{

QJsonObject providerUsageToJson(const ProviderUsage &usage)
{
    QJsonObject obj;
    if (usage.inputTokens >= 0)
    {
        obj[QStringLiteral("inputTokens")] = usage.inputTokens;
    }
    if (usage.outputTokens >= 0)
    {
        obj[QStringLiteral("outputTokens")] = usage.outputTokens;
    }
    if (usage.totalTokens >= 0)
    {
        obj[QStringLiteral("totalTokens")] = usage.totalTokens;
    }
    if (usage.reasoningTokens >= 0)
    {
        obj[QStringLiteral("reasoningTokens")] = usage.reasoningTokens;
    }
    if (usage.cachedInputTokens >= 0)
    {
        obj[QStringLiteral("cachedInputTokens")] = usage.cachedInputTokens;
    }
    return obj;
}

ProviderUsage providerUsageFromJson(const QJsonObject &obj)
{
    ProviderUsage usage;
    usage.inputTokens = obj.value(QStringLiteral("inputTokens")).toInt(-1);
    usage.outputTokens = obj.value(QStringLiteral("outputTokens")).toInt(-1);
    usage.totalTokens = obj.value(QStringLiteral("totalTokens")).toInt(-1);
    usage.reasoningTokens = obj.value(QStringLiteral("reasoningTokens")).toInt(-1);
    usage.cachedInputTokens = obj.value(QStringLiteral("cachedInputTokens")).toInt(-1);
    return usage;
}

QJsonArray memoryItemsToJson(const QList<MemoryItem> &items)
{
    QJsonArray array;
    for (const MemoryItem &item : items)
    {
        array.append(item.toJson());
    }
    return array;
}

QJsonArray messagesToJson(const QList<ContextMessage> &items)
{
    QJsonArray array;
    for (const ContextMessage &item : items)
    {
        array.append(item.toJson());
    }
    return array;
}

QJsonArray artifactsToJson(const QList<ArtifactRecord> &items)
{
    QJsonArray array;
    for (const ArtifactRecord &item : items)
    {
        array.append(item.toJson());
    }
    return array;
}

QJsonArray providerMessagesToJson(const QList<ChatMessage> &items)
{
    QJsonArray array;
    for (const ChatMessage &item : items)
    {
        array.append(item.toJson());
    }
    return array;
}

}  // namespace

QJsonObject ContextMessage::toJson() const
{
    return QJsonObject{{QStringLiteral("messageId"), messageId},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("conversationId"), conversationId},
                       {QStringLiteral("runId"), runId},
                       {QStringLiteral("sequence"), sequence},
                       {QStringLiteral("role"), role},
                       {QStringLiteral("source"), source},
                       {QStringLiteral("content"), content},
                       {QStringLiteral("createdAt"), createdAt},
                       {QStringLiteral("tokenEstimate"), tokenEstimate},
                       {QStringLiteral("metadata"), metadata}};
}

ContextMessage ContextMessage::fromJson(const QJsonObject &obj)
{
    ContextMessage message;
    message.messageId = obj.value(QStringLiteral("messageId")).toString();
    message.workspaceId = obj.value(QStringLiteral("workspaceId")).toString();
    message.conversationId = obj.value(QStringLiteral("conversationId")).toString();
    message.runId = obj.value(QStringLiteral("runId")).toString();
    message.sequence = obj.value(QStringLiteral("sequence")).toInt();
    message.role = obj.value(QStringLiteral("role")).toString();
    message.source = obj.value(QStringLiteral("source")).toString();
    message.content = obj.value(QStringLiteral("content")).toString();
    message.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    message.tokenEstimate = obj.value(QStringLiteral("tokenEstimate")).toInt();
    message.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return message;
}

ChatMessage ContextMessage::toChatMessage() const
{
    return {role, content};
}

bool ContextSummary::isValid() const
{
    return !summaryId.isEmpty() && !conversationId.isEmpty() && endSequence >= startSequence;
}

QJsonObject ContextSummary::toJson() const
{
    return QJsonObject{{QStringLiteral("summaryId"), summaryId},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("conversationId"), conversationId},
                       {QStringLiteral("version"), version},
                       {QStringLiteral("startSequence"), startSequence},
                       {QStringLiteral("endSequence"), endSequence},
                       {QStringLiteral("createdAt"), createdAt},
                       {QStringLiteral("content"), content},
                       {QStringLiteral("tokenEstimate"), tokenEstimate},
                       {QStringLiteral("metadata"), metadata}};
}

ContextSummary ContextSummary::fromJson(const QJsonObject &obj)
{
    ContextSummary summary;
    summary.summaryId = obj.value(QStringLiteral("summaryId")).toString();
    summary.workspaceId = obj.value(QStringLiteral("workspaceId")).toString();
    summary.conversationId = obj.value(QStringLiteral("conversationId")).toString();
    summary.version = obj.value(QStringLiteral("version")).toInt();
    summary.startSequence = obj.value(QStringLiteral("startSequence")).toInt();
    summary.endSequence = obj.value(QStringLiteral("endSequence")).toInt();
    summary.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    summary.content = obj.value(QStringLiteral("content")).toString();
    summary.tokenEstimate = obj.value(QStringLiteral("tokenEstimate")).toInt();
    summary.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return summary;
}

QJsonObject MemoryItem::toJson() const
{
    return QJsonObject{{QStringLiteral("memoryId"), memoryId},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("key"), key},
                       {QStringLiteral("category"), category},
                       {QStringLiteral("value"), value},
                       {QStringLiteral("source"), source},
                       {QStringLiteral("updatedAt"), updatedAt},
                       {QStringLiteral("importance"), importance},
                       {QStringLiteral("tokenEstimate"), tokenEstimate},
                       {QStringLiteral("metadata"), metadata}};
}

MemoryItem MemoryItem::fromJson(const QJsonObject &obj)
{
    MemoryItem item;
    item.memoryId = obj.value(QStringLiteral("memoryId")).toString();
    item.workspaceId = obj.value(QStringLiteral("workspaceId")).toString();
    item.key = obj.value(QStringLiteral("key")).toString();
    item.category = obj.value(QStringLiteral("category")).toString();
    item.value = obj.value(QStringLiteral("value")).toString();
    item.source = obj.value(QStringLiteral("source")).toString();
    item.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
    item.importance = obj.value(QStringLiteral("importance")).toInt();
    item.tokenEstimate = obj.value(QStringLiteral("tokenEstimate")).toInt();
    item.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return item;
}

QJsonObject ArtifactRecord::toJson() const
{
    return QJsonObject{{QStringLiteral("artifactId"), artifactId},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("conversationId"), conversationId},
                       {QStringLiteral("runId"), runId},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("storagePath"), storagePath},
                       {QStringLiteral("preview"), preview},
                       {QStringLiteral("createdAt"), createdAt},
                       {QStringLiteral("tokenEstimate"), tokenEstimate},
                       {QStringLiteral("metadata"), metadata}};
}

ArtifactRecord ArtifactRecord::fromJson(const QJsonObject &obj)
{
    ArtifactRecord artifact;
    artifact.artifactId = obj.value(QStringLiteral("artifactId")).toString();
    artifact.workspaceId = obj.value(QStringLiteral("workspaceId")).toString();
    artifact.conversationId = obj.value(QStringLiteral("conversationId")).toString();
    artifact.runId = obj.value(QStringLiteral("runId")).toString();
    artifact.kind = obj.value(QStringLiteral("kind")).toString();
    artifact.title = obj.value(QStringLiteral("title")).toString();
    artifact.storagePath = obj.value(QStringLiteral("storagePath")).toString();
    artifact.preview = obj.value(QStringLiteral("preview")).toString();
    artifact.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    artifact.tokenEstimate = obj.value(QStringLiteral("tokenEstimate")).toInt();
    artifact.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return artifact;
}

bool ConversationRecord::isValid() const
{
    return !conversationId.isEmpty() && !workspaceId.isEmpty();
}

QJsonObject ConversationRecord::toJson() const
{
    return QJsonObject{{QStringLiteral("conversationId"), conversationId},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("workspaceRoot"), workspaceRoot},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("createdAt"), createdAt},
                       {QStringLiteral("updatedAt"), updatedAt},
                       {QStringLiteral("lastMessageSequence"), lastMessageSequence},
                       {QStringLiteral("metadata"), metadata}};
}

ConversationRecord ConversationRecord::fromJson(const QJsonObject &obj)
{
    ConversationRecord conversation;
    conversation.conversationId = obj.value(QStringLiteral("conversationId")).toString();
    conversation.workspaceId = obj.value(QStringLiteral("workspaceId")).toString();
    conversation.workspaceRoot = obj.value(QStringLiteral("workspaceRoot")).toString();
    conversation.title = obj.value(QStringLiteral("title")).toString();
    conversation.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    conversation.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
    conversation.lastMessageSequence = obj.value(QStringLiteral("lastMessageSequence")).toInt();
    conversation.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return conversation;
}

QJsonObject RunRecord::toJson() const
{
    return QJsonObject{{QStringLiteral("runId"), runId},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("conversationId"), conversationId},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("providerId"), providerId},
                       {QStringLiteral("model"), model},
                       {QStringLiteral("reasoningEffort"), reasoningEffort},
                       {QStringLiteral("thinkingLevel"), thinkingLevel},
                       {QStringLiteral("dryRun"), dryRun},
                       {QStringLiteral("status"), status},
                       {QStringLiteral("startedAt"), startedAt},
                       {QStringLiteral("finishedAt"), finishedAt},
                       {QStringLiteral("usage"), providerUsageToJson(usage)},
                       {QStringLiteral("metadata"), metadata}};
}

RunRecord RunRecord::fromJson(const QJsonObject &obj)
{
    RunRecord run;
    run.runId = obj.value(QStringLiteral("runId")).toString();
    run.workspaceId = obj.value(QStringLiteral("workspaceId")).toString();
    run.conversationId = obj.value(QStringLiteral("conversationId")).toString();
    run.kind = obj.value(QStringLiteral("kind")).toString();
    run.providerId = obj.value(QStringLiteral("providerId")).toString();
    run.model = obj.value(QStringLiteral("model")).toString();
    run.reasoningEffort = obj.value(QStringLiteral("reasoningEffort")).toString();
    run.thinkingLevel = obj.value(QStringLiteral("thinkingLevel")).toString();
    run.dryRun = obj.value(QStringLiteral("dryRun")).toBool(true);
    run.status = obj.value(QStringLiteral("status")).toString();
    run.startedAt = obj.value(QStringLiteral("startedAt")).toString();
    run.finishedAt = obj.value(QStringLiteral("finishedAt")).toString();
    run.usage = providerUsageFromJson(obj.value(QStringLiteral("usage")).toObject());
    run.metadata = obj.value(QStringLiteral("metadata")).toObject();
    return run;
}

QJsonObject ContextBudget::toJson() const
{
    return QJsonObject{{QStringLiteral("totalTokens"), totalTokens},
                       {QStringLiteral("memoryTokens"), memoryTokens},
                       {QStringLiteral("summaryTokens"), summaryTokens},
                       {QStringLiteral("recentMessageTokens"), recentMessageTokens},
                       {QStringLiteral("artifactTokens"), artifactTokens},
                       {QStringLiteral("maxRecentMessages"), maxRecentMessages},
                       {QStringLiteral("maxArtifacts"), maxArtifacts}};
}

QJsonObject ContextEnvelope::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("workspaceId")] = workspaceId;
    obj[QStringLiteral("conversationId")] = conversationId;
    obj[QStringLiteral("budget")] = budget.toJson();
    obj[QStringLiteral("memoryItems")] = memoryItemsToJson(memoryItems);
    obj[QStringLiteral("summary")] = summary.toJson();
    obj[QStringLiteral("recentMessages")] = messagesToJson(recentMessages);
    obj[QStringLiteral("artifacts")] = artifactsToJson(artifacts);
    obj[QStringLiteral("providerMessages")] = providerMessagesToJson(providerMessages);
    return obj;
}

int estimateTokenCount(const QString &text)
{
    if (text.isEmpty() == true)
    {
        return 0;
    }
    const qsizetype estimated = (text.size() + 3) / 4;
    return qMax(1, static_cast<int>(estimated));
}

QString truncateForContext(const QString &text, int maxChars)
{
    if (maxChars <= 0)
    {
        return {};
    }
    if (text.size() <= maxChars)
    {
        return text;
    }
    return text.left(maxChars) + QStringLiteral("\n… [truncated]");
}

QString newContextId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString nowUtcIsoString()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString requestKindName(ContextRequestKind kind)
{
    switch (kind)
    {
        case ContextRequestKind::AgentChat:
            return QStringLiteral("agent");
        case ContextRequestKind::Ask:
            return QStringLiteral("ask");
        case ContextRequestKind::Completion:
            return QStringLiteral("completion");
    }
    return QStringLiteral("agent");
}

ContextBudget budgetForRequest(ContextRequestKind kind, int maxOutputTokens)
{
    ContextBudget budget;
    switch (kind)
    {
        case ContextRequestKind::Completion:
            budget.totalTokens = 320;
            budget.memoryTokens = 80;
            budget.summaryTokens = 120;
            budget.recentMessageTokens = 80;
            budget.artifactTokens = 40;
            budget.maxRecentMessages = 2;
            budget.maxArtifacts = 1;
            break;
        case ContextRequestKind::Ask:
            budget.totalTokens = qBound(1200, maxOutputTokens * 2, 8000);
            budget.memoryTokens = qMax(200, budget.totalTokens / 6);
            budget.summaryTokens = qMax(300, budget.totalTokens / 3);
            budget.recentMessageTokens = qMax(400, budget.totalTokens / 3);
            budget.artifactTokens = qMax(150, budget.totalTokens / 8);
            budget.maxRecentMessages = 10;
            budget.maxArtifacts = 2;
            break;
        case ContextRequestKind::AgentChat:
            budget.totalTokens = qBound(2000, maxOutputTokens * 3, 12000);
            budget.memoryTokens = qMax(300, budget.totalTokens / 6);
            budget.summaryTokens = qMax(500, budget.totalTokens / 3);
            budget.recentMessageTokens = qMax(700, budget.totalTokens / 3);
            budget.artifactTokens = qMax(250, budget.totalTokens / 10);
            budget.maxRecentMessages = 14;
            budget.maxArtifacts = 3;
            break;
    }
    return budget;
}

QString defaultArtifactFileSuffix(const QString &kind)
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
