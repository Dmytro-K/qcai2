#pragma once

#include "../models/AgentMessages.h"
#include "../providers/ProviderUsage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace qcai2
{

struct ContextMessage
{
    QString messageId;
    QString workspaceId;
    QString conversationId;
    QString runId;
    int sequence = 0;
    QString role;
    QString source;
    QString content;
    QString createdAt;
    int tokenEstimate = 0;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static ContextMessage fromJson(const QJsonObject &obj);
    ChatMessage toChatMessage() const;
};

struct ContextSummary
{
    QString summaryId;
    QString workspaceId;
    QString conversationId;
    int version = 0;
    int startSequence = 0;
    int endSequence = 0;
    QString createdAt;
    QString content;
    int tokenEstimate = 0;
    QJsonObject metadata;

    bool isValid() const;
    QJsonObject toJson() const;
    static ContextSummary fromJson(const QJsonObject &obj);
};

struct MemoryItem
{
    QString memoryId;
    QString workspaceId;
    QString key;
    QString category;
    QString value;
    QString source;
    QString updatedAt;
    int importance = 0;
    int tokenEstimate = 0;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static MemoryItem fromJson(const QJsonObject &obj);
};

struct ArtifactRecord
{
    QString artifactId;
    QString workspaceId;
    QString conversationId;
    QString runId;
    QString kind;
    QString title;
    QString storagePath;
    QString preview;
    QString createdAt;
    int tokenEstimate = 0;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static ArtifactRecord fromJson(const QJsonObject &obj);
};

struct ConversationRecord
{
    QString conversationId;
    QString workspaceId;
    QString workspaceRoot;
    QString title;
    QString createdAt;
    QString updatedAt;
    int lastMessageSequence = 0;
    QJsonObject metadata;

    bool isValid() const;
    QJsonObject toJson() const;
    static ConversationRecord fromJson(const QJsonObject &obj);
};

struct RunRecord
{
    QString runId;
    QString workspaceId;
    QString conversationId;
    QString kind;
    QString providerId;
    QString model;
    QString reasoningEffort;
    QString thinkingLevel;
    bool dryRun = true;
    QString status;
    QString startedAt;
    QString finishedAt;
    ProviderUsage usage;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static RunRecord fromJson(const QJsonObject &obj);
};

enum class ContextRequestKind : std::uint8_t
{
    AgentChat,
    Ask,
    Completion,
};

struct ContextBudget
{
    int totalTokens = 0;
    int memoryTokens = 0;
    int summaryTokens = 0;
    int recentMessageTokens = 0;
    int artifactTokens = 0;
    int maxRecentMessages = 0;
    int maxArtifacts = 0;

    QJsonObject toJson() const;
};

struct ContextEnvelope
{
    QString workspaceId;
    QString conversationId;
    ContextBudget budget;
    QList<MemoryItem> memoryItems;
    ContextSummary summary;
    QList<ContextMessage> recentMessages;
    QList<ArtifactRecord> artifacts;
    QList<ChatMessage> providerMessages;

    QJsonObject toJson() const;
};

int estimateTokenCount(const QString &text);
QString truncateForContext(const QString &text, int maxChars);
QString newContextId();
QString nowUtcIsoString();
QString requestKindName(ContextRequestKind kind);
ContextBudget budgetForRequest(ContextRequestKind kind, int maxOutputTokens);
QString defaultArtifactFileSuffix(const QString &kind);

}  // namespace qcai2
