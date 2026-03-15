#pragma once

#include "ChatContext.h"

namespace qcai2
{

class ChatContextStore
{
public:
    ChatContextStore();
    ~ChatContextStore();

    bool open(const QString &databasePath, const QString &artifactDirPath,
              QString *error = nullptr);
    void close();

    bool isOpen() const;
    QString databasePath() const;
    QString artifactDirPath() const;

    bool ensureConversation(ConversationRecord *conversation, QString *error = nullptr);
    ConversationRecord conversation(const QString &conversationId, QString *error = nullptr) const;

    bool saveRun(const RunRecord &run, QString *error = nullptr);
    bool updateRun(const RunRecord &run, QString *error = nullptr);

    bool appendMessage(ContextMessage *message, QString *error = nullptr);
    bool upsertMemoryItem(MemoryItem *item, QString *error = nullptr);
    bool addSummary(ContextSummary *summary, QString *error = nullptr);
    bool addArtifact(ArtifactRecord *artifact, const QString &content, QString *error = nullptr);

    QList<ContextMessage> recentMessages(const QString &conversationId, int limit,
                                         QString *error = nullptr) const;
    QList<ContextMessage> messagesRange(const QString &conversationId, int startSequenceExclusive,
                                        int endSequenceInclusive, QString *error = nullptr) const;
    QList<MemoryItem> memoryItems(const QString &workspaceId, int limit,
                                  QString *error = nullptr) const;
    QList<ArtifactRecord> recentArtifacts(const QString &conversationId, int limit,
                                          QString *error = nullptr) const;

    ContextSummary latestSummary(const QString &conversationId, QString *error = nullptr) const;
    int latestSummaryVersion(const QString &conversationId, QString *error = nullptr) const;
    int latestMessageSequence(const QString &conversationId, QString *error = nullptr) const;

private:
    bool initializeLayout(QString *error);
    bool ensureOpen(QString *error) const;
    QString conversationsDirectoryPath() const;
    QString conversationDirectoryPath(const QString &conversationId) const;
    QString conversationStatePath(const QString &conversationId) const;
    QString conversationMessagesPath(const QString &conversationId) const;
    QString conversationSummariesPath(const QString &conversationId) const;
    QString conversationLatestSummaryPath(const QString &conversationId) const;
    QString conversationArtifactsPath(const QString &conversationId) const;
    QString runsDirectoryPath() const;
    QString runPath(const QString &runId) const;
    QString memoryPath() const;
    QString formatPath() const;

    QString m_databasePath;
    QString m_artifactDirPath;
    bool m_isOpen = false;
};

}  // namespace qcai2
