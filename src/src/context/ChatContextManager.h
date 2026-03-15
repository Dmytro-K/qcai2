#pragma once

#include "../context/EditorContext.h"
#include "ChatContextStore.h"

#include <QHash>
#include <QObject>

#include <memory>

namespace qcai2
{

struct Settings;

class ChatContextManager : public QObject
{
    Q_OBJECT

public:
    explicit ChatContextManager(QObject *parent = nullptr);

    bool setActiveWorkspace(const QString &workspaceId, const QString &workspaceRoot,
                            const QString &conversationId = {}, QString *error = nullptr);
    QString activeWorkspaceId() const;
    QString activeWorkspaceRoot() const;
    QString activeConversationId() const;

    QString ensureActiveConversation(QString *error = nullptr);
    QString startNewConversation(const QString &title = {}, QString *error = nullptr);

    void syncWorkspaceState(const EditorContext::Snapshot &snapshot, const Settings &settings,
                            QString *error = nullptr);

    QString beginRun(ContextRequestKind kind, const QString &providerId, const QString &model,
                     const QString &reasoningEffort, const QString &thinkingLevel, bool dryRun,
                     const QJsonObject &metadata = {}, QString *error = nullptr);
    bool finishRun(const QString &runId, const QString &status, const ProviderUsage &usage,
                   const QJsonObject &metadata = {}, QString *error = nullptr);

    bool appendUserMessage(const QString &runId, const QString &content, const QString &source,
                           const QJsonObject &metadata = {}, QString *error = nullptr);
    bool appendAssistantMessage(const QString &runId, const QString &content,
                                const QString &source, const QJsonObject &metadata = {},
                                QString *error = nullptr);
    bool appendArtifact(const QString &runId, const QString &kind, const QString &title,
                        const QString &content, const QJsonObject &metadata = {},
                        QString *error = nullptr);

    ContextEnvelope buildContextEnvelope(ContextRequestKind kind, const QString &systemInstruction,
                                         const QStringList &dynamicSystemMessages,
                                         int maxOutputTokens, QString *error = nullptr) const;
    QString buildCompletionContextBlock(const QString &filePath, int maxOutputTokens,
                                        QString *error = nullptr) const;

    bool maybeRefreshSummary(QString *error = nullptr);

    QString databasePath() const;
    QString artifactDirectoryPath() const;

private:
    bool ensureStoreOpen(QString *error = nullptr) const;
    bool ensureConversationAvailable(QString *error = nullptr);
    bool appendMessage(const QString &runId, const QString &role, const QString &content,
                       const QString &source, const QJsonObject &metadata,
                       QString *error = nullptr);

    QString conversationSummaryBlock(const ContextSummary &summary, int maxTokens) const;
    QString memoryBlock(const QList<MemoryItem> &items, int maxTokens) const;
    QString artifactBlock(const QList<ArtifactRecord> &artifacts, int maxTokens) const;
    QString recentMessagesBlock(const QList<ContextMessage> &messages, int maxTokens) const;
    QString renderMessageBullet(const ContextMessage &message, int maxChars) const;
    QString renderArtifactBullet(const ArtifactRecord &artifact, int maxChars) const;
    QList<MemoryItem> selectMemoryItems(const ContextBudget &budget,
                                        QString *error = nullptr) const;
    QList<ArtifactRecord> selectArtifacts(const ContextBudget &budget,
                                          QString *error = nullptr) const;
    QList<ContextMessage> selectRecentMessages(const ContextBudget &budget,
                                               QString *error = nullptr) const;
    ContextSummary composeNextSummary(const ContextSummary &previous,
                                      const QList<ContextMessage> &chunk,
                                      const QList<ArtifactRecord> &artifacts,
                                      QString *error = nullptr) const;

    QString databasePathForWorkspace() const;
    QString artifactPathForWorkspace() const;

    mutable std::unique_ptr<ChatContextStore> m_store;
    QString m_workspaceId;
    QString m_workspaceRoot;
    ConversationRecord m_conversation;
    QHash<QString, RunRecord> m_activeRuns;
};

}  // namespace qcai2
