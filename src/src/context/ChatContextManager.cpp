#include "ChatContextManager.h"

#include "../settings/Settings.h"

#include <QDir>
#include <QFileInfo>

namespace qcai2
{

namespace
{

constexpr int kSummaryRefreshMessageThreshold = 12;
constexpr int kSummaryRefreshTokenThreshold = 900;
constexpr int kSummaryChunkMaxMessages = 24;
constexpr int kSummaryChunkMaxTokens = 1400;
constexpr int kSummaryCarryForwardChars = 1800;
constexpr int kSummaryMaxChars = 4200;

QString sidecarPathForWorkspace(const QString &workspaceRoot, const QString &fileName)
{
    return QDir(workspaceRoot).filePath(QStringLiteral(".qcai2/%1").arg(fileName));
}

QString knownOrUnknown(const QString &value)
{
    return value.isEmpty() == true ? QStringLiteral("<unknown>") : value;
}

QJsonObject mergeJsonObjects(const QJsonObject &base, const QJsonObject &extra)
{
    QJsonObject merged = base;
    for (auto it = extra.begin(); it != extra.end(); ++it)
    {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

MemoryItem makeMemoryItem(const QString &workspaceId, const QString &key, const QString &category,
                          const QString &value, const QString &source, int importance,
                          const QJsonObject &metadata = {})
{
    MemoryItem item;
    item.workspaceId = workspaceId;
    item.key = key;
    item.category = category;
    item.value = value;
    item.source = source;
    item.importance = importance;
    item.metadata = metadata;
    return item;
}

bool propagateError(QString *target, const QString &error)
{
    if (error.isEmpty() == true)
    {
        return false;
    }
    if (target != nullptr)
    {
        *target = error;
    }
    return true;
}

}  // namespace

ChatContextManager::ChatContextManager(QObject *parent) : QObject(parent)
{
    m_store = std::make_unique<ChatContextStore>();
}

bool ChatContextManager::setActiveWorkspace(const QString &workspaceId,
                                            const QString &workspaceRoot,
                                            const QString &conversationId, QString *error)
{
    m_workspaceId = workspaceId;
    m_workspaceRoot = workspaceRoot;
    m_conversation = {};
    m_conversation.workspaceId = workspaceId;
    m_conversation.workspaceRoot = workspaceRoot;
    m_conversation.conversationId = conversationId;
    m_activeRuns.clear();

    if (ensureStoreOpen(error) == false)
    {
        return false;
    }

    if (conversationId.isEmpty() == false)
    {
        const ConversationRecord storedConversation = m_store->conversation(conversationId, error);
        if (storedConversation.isValid() == true)
        {
            m_conversation = storedConversation;
        }
        else
        {
            m_conversation.workspaceId = workspaceId;
            m_conversation.workspaceRoot = workspaceRoot;
            m_conversation.conversationId = conversationId;
        }
    }

    return true;
}

QString ChatContextManager::activeWorkspaceId() const
{
    return m_workspaceId;
}

QString ChatContextManager::activeWorkspaceRoot() const
{
    return m_workspaceRoot;
}

QString ChatContextManager::activeConversationId() const
{
    return m_conversation.conversationId;
}

QString ChatContextManager::ensureActiveConversation(QString *error)
{
    if (ensureConversationAvailable(error) == false)
    {
        return {};
    }
    return m_conversation.conversationId;
}

QString ChatContextManager::startNewConversation(const QString &title, QString *error)
{
    if (m_workspaceId.isEmpty() == true || m_workspaceRoot.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Active workspace is not set");
        }
        return {};
    }

    m_conversation = {};
    m_conversation.conversationId = newContextId();
    m_conversation.workspaceId = m_workspaceId;
    m_conversation.workspaceRoot = m_workspaceRoot;
    m_conversation.title = title.isNull() == true ? QStringLiteral("") : title;
    m_conversation.createdAt = nowUtcIsoString();
    m_conversation.updatedAt = m_conversation.createdAt;

    if (ensureStoreOpen(error) == false)
    {
        return {};
    }
    if (m_store->ensureConversation(&m_conversation, error) == false)
    {
        return {};
    }
    return m_conversation.conversationId;
}

void ChatContextManager::syncWorkspaceState(const EditorContext::Snapshot &snapshot,
                                            const Settings &settings, QString *error)
{
    if (ensureStoreOpen(error) == false)
    {
        return;
    }
    if (m_workspaceId.isEmpty() == true)
    {
        return;
    }

    const QString codingStylePath =
        QDir(m_workspaceRoot).filePath(QStringLiteral("docs/CODING_STYLE.md"));
    const bool codingStyleExists = QFileInfo::exists(codingStylePath);

    QList<MemoryItem> items;
    items.append(makeMemoryItem(m_workspaceId, QStringLiteral("workspace_root"),
                                QStringLiteral("workspace"), knownOrUnknown(m_workspaceRoot),
                                QStringLiteral("editor_context"), 100));
    items.append(makeMemoryItem(m_workspaceId, QStringLiteral("build_directory"),
                                QStringLiteral("workspace"), knownOrUnknown(snapshot.buildDir),
                                QStringLiteral("editor_context"), 95));
    items.append(makeMemoryItem(
        m_workspaceId, QStringLiteral("active_project"), QStringLiteral("workspace"),
        knownOrUnknown(snapshot.projectFilePath), QStringLiteral("editor_context"), 95));
    if (snapshot.filePath.isEmpty() == false)
    {
        items.append(makeMemoryItem(m_workspaceId, QStringLiteral("active_file"),
                                    QStringLiteral("workspace"), snapshot.filePath,
                                    QStringLiteral("editor_context"), 80));
    }
    if (codingStyleExists == true)
    {
        items.append(makeMemoryItem(m_workspaceId, QStringLiteral("coding_style"),
                                    QStringLiteral("preference"),
                                    QStringLiteral("Follow %1").arg(codingStylePath),
                                    QStringLiteral("workspace_doc"), 90));
    }
    items.append(makeMemoryItem(m_workspaceId, QStringLiteral("safety_rules"),
                                QStringLiteral("constraint"),
                                QStringLiteral("Do not reveal secrets, avoid destructive commands "
                                               "without approval, and surface errors explicitly."),
                                QStringLiteral("built_in"), 85));
    items.append(makeMemoryItem(
        m_workspaceId, QStringLiteral("agent_defaults"), QStringLiteral("preference"),
        QStringLiteral("model=%1; reasoning=%2; thinking=%3; dryRunDefault=%4")
            .arg(settings.modelName, settings.reasoningEffort, settings.thinkingLevel,
                 settings.dryRunDefault == true ? QStringLiteral("true")
                                                : QStringLiteral("false")),
        QStringLiteral("settings"), 70));
    items.append(makeMemoryItem(
        m_workspaceId, QStringLiteral("completion_defaults"), QStringLiteral("preference"),
        QStringLiteral("model=%1; reasoning=%2; thinking=%3")
            .arg(settings.completionModel.isEmpty() == true ? settings.modelName
                                                            : settings.completionModel,
                 settings.completionReasoningEffort, settings.completionThinkingLevel),
        QStringLiteral("settings"), 60));

    for (MemoryItem &item : items)
    {
        if (m_store->upsertMemoryItem(&item, error) == false)
        {
            return;
        }
    }
}

QString ChatContextManager::beginRun(ContextRequestKind kind, const QString &providerId,
                                     const QString &model, const QString &reasoningEffort,
                                     const QString &thinkingLevel, bool dryRun,
                                     const QJsonObject &metadata, QString *error)
{
    if (ensureConversationAvailable(error) == false)
    {
        return {};
    }

    RunRecord run;
    run.runId = newContextId();
    run.workspaceId = m_workspaceId;
    run.conversationId = m_conversation.conversationId;
    run.kind = requestKindName(kind);
    run.providerId = providerId;
    run.model = model;
    run.reasoningEffort = reasoningEffort;
    run.thinkingLevel = thinkingLevel;
    run.dryRun = dryRun;
    run.status = QStringLiteral("running");
    run.startedAt = nowUtcIsoString();
    run.finishedAt = QStringLiteral("");
    run.metadata = metadata;

    if (m_store->saveRun(run, error) == false)
    {
        return {};
    }

    m_activeRuns.insert(run.runId, run);
    return run.runId;
}

bool ChatContextManager::finishRun(const QString &runId, const QString &status,
                                   const ProviderUsage &usage, const QJsonObject &metadata,
                                   QString *error)
{
    if (ensureStoreOpen(error) == false)
    {
        return false;
    }

    RunRecord run = m_activeRuns.value(runId);
    if (run.runId.isEmpty() == true)
    {
        run.runId = runId;
        run.status = status;
    }

    run.status = status;
    run.finishedAt = nowUtcIsoString();
    run.usage = usage;
    run.metadata = mergeJsonObjects(run.metadata, metadata);

    const bool updated = m_store->updateRun(run, error);
    if (updated == false && error != nullptr && error->isEmpty() == true)
    {
        *error = QStringLiteral("Run record was not updated");
    }
    m_activeRuns.remove(runId);
    return updated;
}

bool ChatContextManager::appendUserMessage(const QString &runId, const QString &content,
                                           const QString &source, const QJsonObject &metadata,
                                           QString *error)
{
    return appendMessage(runId, QStringLiteral("user"), content, source, metadata, error);
}

bool ChatContextManager::appendAssistantMessage(const QString &runId, const QString &content,
                                                const QString &source, const QJsonObject &metadata,
                                                QString *error)
{
    return appendMessage(runId, QStringLiteral("assistant"), content, source, metadata, error);
}

bool ChatContextManager::appendArtifact(const QString &runId, const QString &kind,
                                        const QString &title, const QString &content,
                                        const QJsonObject &metadata, QString *error)
{
    if (ensureConversationAvailable(error) == false)
    {
        return false;
    }

    ArtifactRecord artifact;
    artifact.workspaceId = m_workspaceId;
    artifact.conversationId = m_conversation.conversationId;
    artifact.runId = runId;
    artifact.kind = kind;
    artifact.title = title;
    artifact.preview = truncateForContext(content, 1000);
    artifact.metadata = metadata;

    return m_store->addArtifact(&artifact, content, error);
}

ContextEnvelope ChatContextManager::buildContextEnvelope(ContextRequestKind kind,
                                                         const QString &systemInstruction,
                                                         const QStringList &dynamicSystemMessages,
                                                         int maxOutputTokens, QString *error) const
{
    ContextEnvelope envelope;
    if (ensureStoreOpen(error) == false)
    {
        return envelope;
    }
    if (m_conversation.isValid() == false)
    {
        return envelope;
    }

    envelope.workspaceId = m_workspaceId;
    envelope.conversationId = m_conversation.conversationId;
    envelope.budget = budgetForRequest(kind, maxOutputTokens);

    QString localError;
    envelope.memoryItems = selectMemoryItems(envelope.budget, &localError);
    if (propagateError(error, localError) == true)
    {
        return ContextEnvelope{};
    }

    envelope.summary = m_store->latestSummary(m_conversation.conversationId, &localError);
    if (propagateError(error, localError) == true)
    {
        return ContextEnvelope{};
    }

    envelope.artifacts = selectArtifacts(envelope.budget, &localError);
    if (propagateError(error, localError) == true)
    {
        return ContextEnvelope{};
    }

    envelope.recentMessages = selectRecentMessages(envelope.budget, &localError);
    if (propagateError(error, localError) == true)
    {
        return ContextEnvelope{};
    }

    if (systemInstruction.isEmpty() == false)
    {
        envelope.providerMessages.append(ChatMessage{QStringLiteral("system"), systemInstruction});
    }

    for (const QString &message : dynamicSystemMessages)
    {
        if (message.isEmpty() == false)
        {
            envelope.providerMessages.append(ChatMessage{QStringLiteral("system"), message});
        }
    }

    const QString memoryText = memoryBlock(envelope.memoryItems, envelope.budget.memoryTokens);
    if (memoryText.isEmpty() == false)
    {
        envelope.providerMessages.append(ChatMessage{QStringLiteral("system"), memoryText});
    }

    const QString summaryText =
        conversationSummaryBlock(envelope.summary, envelope.budget.summaryTokens);
    if (summaryText.isEmpty() == false)
    {
        envelope.providerMessages.append(ChatMessage{QStringLiteral("system"), summaryText});
    }

    const QString artifactText = artifactBlock(envelope.artifacts, envelope.budget.artifactTokens);
    if (artifactText.isEmpty() == false)
    {
        envelope.providerMessages.append(ChatMessage{QStringLiteral("system"), artifactText});
    }

    for (const ContextMessage &message : envelope.recentMessages)
    {
        envelope.providerMessages.append(message.toChatMessage());
    }

    return envelope;
}

QString ChatContextManager::buildCompletionContextBlock(const QString &filePath,
                                                        int maxOutputTokens, QString *error) const
{
    if (ensureStoreOpen(error) == false)
    {
        return {};
    }
    if (m_conversation.isValid() == false)
    {
        return {};
    }

    const ContextBudget budget = budgetForRequest(ContextRequestKind::Completion, maxOutputTokens);

    QString localError;
    const QList<MemoryItem> memoryItems = selectMemoryItems(budget, &localError);
    if (propagateError(error, localError) == true)
    {
        return {};
    }

    const ContextSummary summary =
        m_store->latestSummary(m_conversation.conversationId, &localError);
    if (propagateError(error, localError) == true)
    {
        return {};
    }

    const QList<ArtifactRecord> artifacts = selectArtifacts(budget, &localError);
    if (propagateError(error, localError) == true)
    {
        return {};
    }

    const QList<ContextMessage> messages = selectRecentMessages(budget, &localError);
    if (propagateError(error, localError) == true)
    {
        return {};
    }

    QStringList parts;
    parts.append(QStringLiteral("Completion context for %1").arg(filePath));

    const QString memoryText = memoryBlock(memoryItems, budget.memoryTokens);
    if (memoryText.isEmpty() == false)
    {
        parts.append(memoryText);
    }

    const QString summaryText = conversationSummaryBlock(summary, budget.summaryTokens);
    if (summaryText.isEmpty() == false)
    {
        parts.append(summaryText);
    }

    const QString messageText = recentMessagesBlock(messages, budget.recentMessageTokens);
    if (messageText.isEmpty() == false)
    {
        parts.append(messageText);
    }

    const QString artifactText = artifactBlock(artifacts, budget.artifactTokens);
    if (artifactText.isEmpty() == false)
    {
        parts.append(artifactText);
    }

    return parts.join(QStringLiteral("\n\n"));
}

bool ChatContextManager::maybeRefreshSummary(QString *error)
{
    if (ensureConversationAvailable(error) == false)
    {
        return false;
    }

    QString localError;
    const ContextSummary latest =
        m_store->latestSummary(m_conversation.conversationId, &localError);
    if (propagateError(error, localError) == true)
    {
        return false;
    }

    const int latestSequence =
        m_store->latestMessageSequence(m_conversation.conversationId, &localError);
    if (propagateError(error, localError) == true)
    {
        return false;
    }
    if (latestSequence <= latest.endSequence)
    {
        return true;
    }

    const QList<ContextMessage> unsummarizedMessages = m_store->messagesRange(
        m_conversation.conversationId, latest.endSequence, latestSequence, &localError);
    if (propagateError(error, localError) == true)
    {
        return false;
    }

    int unsummarizedTokens = 0;
    for (const ContextMessage &message : unsummarizedMessages)
    {
        unsummarizedTokens += message.tokenEstimate;
    }

    if ((unsummarizedMessages.size() < kSummaryRefreshMessageThreshold) &&
        (unsummarizedTokens < kSummaryRefreshTokenThreshold))
    {
        return true;
    }

    QList<ContextMessage> chunk;
    int chunkTokens = 0;
    for (const ContextMessage &message : unsummarizedMessages)
    {
        if ((chunk.size() >= kSummaryChunkMaxMessages) ||
            (((chunkTokens + message.tokenEstimate) > kSummaryChunkMaxTokens) &&
             (chunk.isEmpty() == false)))
        {
            break;
        }
        chunk.append(message);
        chunkTokens += message.tokenEstimate;
    }
    if (chunk.isEmpty() == true)
    {
        return true;
    }

    const QList<ArtifactRecord> artifacts =
        m_store->recentArtifacts(m_conversation.conversationId, 4, &localError);
    if (propagateError(error, localError) == true)
    {
        return false;
    }

    ContextSummary nextSummary = composeNextSummary(latest, chunk, artifacts, &localError);
    if (propagateError(error, localError) == true)
    {
        return false;
    }
    if (nextSummary.isValid() == false)
    {
        return true;
    }

    return m_store->addSummary(&nextSummary, error);
}

QString ChatContextManager::databasePath() const
{
    return databasePathForWorkspace();
}

QString ChatContextManager::artifactDirectoryPath() const
{
    return artifactPathForWorkspace();
}

bool ChatContextManager::ensureStoreOpen(QString *error) const
{
    if (m_workspaceRoot.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Workspace root is not set");
        }
        return false;
    }
    return m_store->open(databasePathForWorkspace(), artifactPathForWorkspace(), error);
}

bool ChatContextManager::ensureConversationAvailable(QString *error)
{
    if (ensureStoreOpen(error) == false)
    {
        return false;
    }
    if (m_conversation.isValid() == false)
    {
        return startNewConversation({}, error).isEmpty() == false;
    }
    return m_store->ensureConversation(&m_conversation, error);
}

bool ChatContextManager::appendMessage(const QString &runId, const QString &role,
                                       const QString &content, const QString &source,
                                       const QJsonObject &metadata, QString *error)
{
    if (content.isEmpty() == true)
    {
        return true;
    }
    if (ensureConversationAvailable(error) == false)
    {
        return false;
    }

    ContextMessage message;
    message.workspaceId = m_workspaceId;
    message.conversationId = m_conversation.conversationId;
    message.runId = runId;
    message.role = role;
    message.source = source;
    message.content = content;
    message.metadata = metadata;
    if (m_store->appendMessage(&message, error) == false)
    {
        return false;
    }

    m_conversation.lastMessageSequence = message.sequence;
    m_conversation.updatedAt = message.createdAt;
    return maybeRefreshSummary(error);
}

QString ChatContextManager::conversationSummaryBlock(const ContextSummary &summary,
                                                     int maxTokens) const
{
    if (summary.isValid() == false || maxTokens <= 0)
    {
        return {};
    }

    return QStringLiteral("Rolling summary (version %1, messages %2-%3):\n%4")
        .arg(summary.version)
        .arg(summary.startSequence)
        .arg(summary.endSequence)
        .arg(truncateForContext(summary.content, maxTokens * 4));
}

QString ChatContextManager::memoryBlock(const QList<MemoryItem> &items, int maxTokens) const
{
    if (items.isEmpty() == true || maxTokens <= 0)
    {
        return {};
    }

    QStringList lines;
    lines.append(QStringLiteral("Stable workspace memory:"));
    int usedTokens = estimateTokenCount(lines.constFirst());
    for (const MemoryItem &item : items)
    {
        const QString line =
            QStringLiteral("- %1 (%2): %3")
                .arg(item.key, item.category, truncateForContext(item.value, 220));
        const int itemTokens = estimateTokenCount(line);
        if (((usedTokens + itemTokens) > maxTokens) == true)
        {
            break;
        }
        lines.append(line);
        usedTokens += itemTokens;
    }
    return lines.join(QStringLiteral("\n"));
}

QString ChatContextManager::artifactBlock(const QList<ArtifactRecord> &artifacts,
                                          int maxTokens) const
{
    if (artifacts.isEmpty() == true || maxTokens <= 0)
    {
        return {};
    }

    QStringList lines;
    lines.append(QStringLiteral("Relevant prior artifacts (referenced, not inlined):"));
    int usedTokens = estimateTokenCount(lines.constFirst());
    for (const ArtifactRecord &artifact : artifacts)
    {
        const QString line = renderArtifactBullet(artifact, 280);
        const int itemTokens = estimateTokenCount(line);
        if (((usedTokens + itemTokens) > maxTokens) == true)
        {
            break;
        }
        lines.append(line);
        usedTokens += itemTokens;
    }
    return lines.join(QStringLiteral("\n"));
}

QString ChatContextManager::recentMessagesBlock(const QList<ContextMessage> &messages,
                                                int maxTokens) const
{
    if (messages.isEmpty() == true || maxTokens <= 0)
    {
        return {};
    }

    QStringList lines;
    lines.append(QStringLiteral("Recent conversation excerpts:"));
    int usedTokens = estimateTokenCount(lines.constFirst());
    for (const ContextMessage &message : messages)
    {
        const QString line = renderMessageBullet(message, 180);
        const int itemTokens = estimateTokenCount(line);
        if (((usedTokens + itemTokens) > maxTokens) == true)
        {
            break;
        }
        lines.append(line);
        usedTokens += itemTokens;
    }
    return lines.join(QStringLiteral("\n"));
}

QString ChatContextManager::renderMessageBullet(const ContextMessage &message, int maxChars) const
{
    return QStringLiteral("- [%1/%2] %3")
        .arg(message.role, message.source,
             truncateForContext(message.content.simplified(), maxChars));
}

QString ChatContextManager::renderArtifactBullet(const ArtifactRecord &artifact,
                                                 int maxChars) const
{
    QString location = artifact.storagePath;
    if (location.isEmpty() == true)
    {
        location = QStringLiteral("preview-only");
    }
    return QStringLiteral("- [%1] %2 (%3): %4")
        .arg(artifact.kind, artifact.title, location,
             truncateForContext(artifact.preview.simplified(), maxChars));
}

QList<MemoryItem> ChatContextManager::selectMemoryItems(const ContextBudget &budget,
                                                        QString *error) const
{
    QList<MemoryItem> selected;
    const QList<MemoryItem> items = m_store->memoryItems(m_workspaceId, 16, error);
    int usedTokens = 0;
    for (const MemoryItem &item : items)
    {
        if (((usedTokens + item.tokenEstimate) > budget.memoryTokens) == true)
        {
            break;
        }
        selected.append(item);
        usedTokens += item.tokenEstimate;
    }
    return selected;
}

QList<ArtifactRecord> ChatContextManager::selectArtifacts(const ContextBudget &budget,
                                                          QString *error) const
{
    QList<ArtifactRecord> selected;
    const QList<ArtifactRecord> artifacts =
        m_store->recentArtifacts(m_conversation.conversationId, budget.maxArtifacts * 3, error);
    int usedTokens = 0;
    for (const ArtifactRecord &artifact : artifacts)
    {
        if (selected.size() >= budget.maxArtifacts)
        {
            break;
        }
        if (((usedTokens + artifact.tokenEstimate) > budget.artifactTokens) == true)
        {
            continue;
        }
        selected.append(artifact);
        usedTokens += artifact.tokenEstimate;
    }
    return selected;
}

QList<ContextMessage> ChatContextManager::selectRecentMessages(const ContextBudget &budget,
                                                               QString *error) const
{
    QList<ContextMessage> selected;
    const QList<ContextMessage> messages = m_store->recentMessages(
        m_conversation.conversationId, budget.maxRecentMessages * 2, error);
    int usedTokens = 0;
    for (qsizetype index = messages.size() - 1; index >= 0; --index)
    {
        const ContextMessage &message = messages.at(static_cast<int>(index));
        if (((usedTokens + message.tokenEstimate) > budget.recentMessageTokens) == true &&
            (selected.isEmpty() == false))
        {
            continue;
        }
        selected.prepend(message);
        usedTokens += message.tokenEstimate;
        if (selected.size() >= budget.maxRecentMessages)
        {
            break;
        }
    }
    return selected;
}

ContextSummary ChatContextManager::composeNextSummary(const ContextSummary &previous,
                                                      const QList<ContextMessage> &chunk,
                                                      const QList<ArtifactRecord> &artifacts,
                                                      QString * /*error*/) const
{
    ContextSummary summary;
    if (chunk.isEmpty() == true)
    {
        return summary;
    }

    QStringList lines;
    lines.append(QStringLiteral("Rolling summary for workspace %1").arg(m_workspaceId));
    if (previous.isValid() == true)
    {
        lines.append(QStringLiteral("Carry-forward summary:"));
        lines.append(truncateForContext(previous.content, kSummaryCarryForwardChars));
    }
    lines.append(QStringLiteral("Newly summarized conversation:"));
    for (const ContextMessage &message : chunk)
    {
        lines.append(renderMessageBullet(message, 260));
    }
    if (artifacts.isEmpty() == false)
    {
        lines.append(QStringLiteral("Referenced artifacts:"));
        for (const ArtifactRecord &artifact : artifacts)
        {
            lines.append(renderArtifactBullet(artifact, 200));
        }
    }

    summary.workspaceId = m_workspaceId;
    summary.conversationId = m_conversation.conversationId;
    summary.summaryId = newContextId();
    summary.version = previous.version + 1;
    summary.startSequence =
        previous.isValid() == true ? previous.startSequence : chunk.constFirst().sequence;
    summary.endSequence = chunk.constLast().sequence;
    summary.createdAt = nowUtcIsoString();
    summary.content = truncateForContext(lines.join(QStringLiteral("\n")), kSummaryMaxChars);
    summary.metadata = QJsonObject{{QStringLiteral("previousSummaryId"), previous.summaryId},
                                   {QStringLiteral("chunkMessageCount"), chunk.size()},
                                   {QStringLiteral("artifactCount"), artifacts.size()}};
    return summary;
}

QString ChatContextManager::databasePathForWorkspace() const
{
    return sidecarPathForWorkspace(m_workspaceRoot, QStringLiteral("chat-context"));
}

QString ChatContextManager::artifactPathForWorkspace() const
{
    return sidecarPathForWorkspace(m_workspaceRoot, QStringLiteral("artifacts"));
}

}  // namespace qcai2
