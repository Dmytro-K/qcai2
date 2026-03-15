#include "ChatContextStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace qcai2
{

namespace
{

constexpr int kChatContextStorageVersion = 1;

QString jsonLine(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool ensureDirectoryExists(const QString &path, QString *error)
{
    if (QDir().mkpath(path) == true)
    {
        return true;
    }

    if (error != nullptr)
    {
        *error = QStringLiteral("Failed to create directory: %1").arg(path);
    }
    return false;
}

bool writeJsonFile(const QString &path, const QJsonObject &obj, QString *error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open JSON file for writing: %1").arg(path);
        }
        return false;
    }

    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to write JSON file: %1").arg(path);
        }
        return false;
    }

    if (file.commit() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to commit JSON file: %1").arg(path);
        }
        return false;
    }
    return true;
}

QJsonObject readJsonFile(const QString &path, bool *exists, QString *error)
{
    if (exists != nullptr)
    {
        *exists = false;
    }

    QFile file(path);
    if (file.exists() == false)
    {
        return {};
    }

    if (exists != nullptr)
    {
        *exists = true;
    }

    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open JSON file for reading: %1").arg(path);
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("JSON file does not contain an object: %1").arg(path);
        }
        return {};
    }

    return document.object();
}

bool appendJsonLine(const QString &path, const QJsonObject &obj, QString *error)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open journal for append: %1").arg(path);
        }
        return false;
    }

    const QByteArray line = jsonLine(obj).toUtf8() + '\n';
    if (file.write(line) != line.size())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to append journal entry: %1").arg(path);
        }
        return false;
    }

    return true;
}

bool parseJsonLine(const QByteArray &line, const QString &path, int lineNumber, QJsonObject *obj,
                   QString *error)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() == true)
    {
        if (obj != nullptr)
        {
            *obj = {};
        }
        return true;
    }

    const QJsonDocument document = QJsonDocument::fromJson(trimmed);
    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Invalid JSONL object in %1 at line %2").arg(path).arg(lineNumber);
        }
        return false;
    }

    if (obj != nullptr)
    {
        *obj = document.object();
    }
    return true;
}

QList<QJsonObject> readAllJsonLines(const QString &path, QString *error)
{
    QList<QJsonObject> objects;

    QFile file(path);
    if (file.exists() == false)
    {
        return objects;
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open journal for reading: %1").arg(path);
        }
        return objects;
    }

    int lineNumber = 0;
    while (file.atEnd() == false)
    {
        const QByteArray line = file.readLine();
        ++lineNumber;

        QJsonObject obj;
        if (parseJsonLine(line, path, lineNumber, &obj, error) == false)
        {
            return {};
        }
        if (obj.isEmpty() == false)
        {
            objects.append(obj);
        }
    }

    return objects;
}

QList<QJsonObject> readRecentJsonLines(const QString &path, int limit, QString *error)
{
    QList<QJsonObject> objects;
    if (limit <= 0)
    {
        return objects;
    }

    QFile file(path);
    if (file.exists() == false)
    {
        return objects;
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open journal for reading: %1").arg(path);
        }
        return {};
    }

    int lineNumber = 0;
    while (file.atEnd() == false)
    {
        const QByteArray line = file.readLine();
        ++lineNumber;

        QJsonObject obj;
        if (parseJsonLine(line, path, lineNumber, &obj, error) == false)
        {
            return {};
        }
        if (obj.isEmpty() == true)
        {
            continue;
        }
        if (objects.size() >= limit)
        {
            objects.removeFirst();
        }
        objects.append(obj);
    }

    return objects;
}

QString nonNullString(const QString &value)
{
    return value.isNull() == true ? QStringLiteral("") : value;
}

QJsonObject memoryStateToJson(const QString &workspaceId, const QList<MemoryItem> &items)
{
    QJsonArray array;
    for (const MemoryItem &item : items)
    {
        array.append(item.toJson());
    }

    return QJsonObject{{QStringLiteral("storageVersion"), kChatContextStorageVersion},
                       {QStringLiteral("workspaceId"), workspaceId},
                       {QStringLiteral("items"), array}};
}

QList<MemoryItem> memoryItemsFromState(const QJsonObject &obj)
{
    QList<MemoryItem> items;
    const QJsonArray array = obj.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : array)
    {
        if (value.isObject() == true)
        {
            items.append(MemoryItem::fromJson(value.toObject()));
        }
    }
    return items;
}

ConversationRecord loadConversationState(const QString &path, QString *error)
{
    bool exists = false;
    const QJsonObject obj = readJsonFile(path, &exists, error);
    if (exists == false)
    {
        return {};
    }
    if (obj.isEmpty() == true)
    {
        return {};
    }
    return ConversationRecord::fromJson(obj);
}

ContextSummary loadLatestSummaryState(const QString &path, QString *error)
{
    bool exists = false;
    const QJsonObject obj = readJsonFile(path, &exists, error);
    if (exists == false)
    {
        return {};
    }
    if (obj.isEmpty() == true)
    {
        return {};
    }
    return ContextSummary::fromJson(obj);
}

}  // namespace

ChatContextStore::ChatContextStore() = default;

ChatContextStore::~ChatContextStore()
{
    close();
}

bool ChatContextStore::open(const QString &databasePath, const QString &artifactDirPath,
                            QString *error)
{
    if ((isOpen() == true) && (m_databasePath == databasePath) &&
        (m_artifactDirPath == artifactDirPath))
    {
        return true;
    }

    close();

    m_databasePath = databasePath;
    m_artifactDirPath = artifactDirPath;

    if (initializeLayout(error) == false)
    {
        close();
        return false;
    }

    m_isOpen = true;
    return true;
}

void ChatContextStore::close()
{
    m_databasePath.clear();
    m_artifactDirPath.clear();
    m_isOpen = false;
}

bool ChatContextStore::isOpen() const
{
    return m_isOpen == true;
}

QString ChatContextStore::databasePath() const
{
    return m_databasePath;
}

QString ChatContextStore::artifactDirPath() const
{
    return m_artifactDirPath;
}

bool ChatContextStore::ensureConversation(ConversationRecord *conversation, QString *error)
{
    if (conversation == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation pointer is null");
        }
        return false;
    }
    if (ensureOpen(error) == false)
    {
        return false;
    }
    if (conversation->isValid() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation is missing required identifiers");
        }
        return false;
    }

    if (conversation->createdAt.isEmpty() == true)
    {
        conversation->createdAt = nowUtcIsoString();
    }
    if (conversation->updatedAt.isEmpty() == true)
    {
        conversation->updatedAt = conversation->createdAt;
    }

    if (ensureDirectoryExists(conversationDirectoryPath(conversation->conversationId), error) ==
        false)
    {
        return false;
    }

    QString localError;
    ConversationRecord storedConversation =
        this->conversation(conversation->conversationId, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (storedConversation.isValid() == false)
    {
        storedConversation = *conversation;
    }

    storedConversation.workspaceId = conversation->workspaceId;
    storedConversation.workspaceRoot = nonNullString(conversation->workspaceRoot);
    storedConversation.title = nonNullString(conversation->title);
    storedConversation.updatedAt = conversation->updatedAt;
    storedConversation.metadata = conversation->metadata;

    if (storedConversation.createdAt.isEmpty() == true)
    {
        storedConversation.createdAt = conversation->createdAt;
    }

    if (writeJsonFile(conversationStatePath(conversation->conversationId),
                      storedConversation.toJson(), error) == false)
    {
        return false;
    }

    *conversation = this->conversation(conversation->conversationId, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    return conversation->isValid();
}

ConversationRecord ChatContextStore::conversation(const QString &conversationId,
                                                  QString *error) const
{
    if (ensureOpen(error) == false)
    {
        return {};
    }

    return loadConversationState(conversationStatePath(conversationId), error);
}

bool ChatContextStore::saveRun(const RunRecord &run, QString *error)
{
    if (ensureOpen(error) == false)
    {
        return false;
    }
    if (run.runId.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Run is missing an identifier");
        }
        return false;
    }

    if (ensureDirectoryExists(runsDirectoryPath(), error) == false)
    {
        return false;
    }

    const QString path = runPath(run.runId);
    if (QFileInfo::exists(path) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Run already exists: %1").arg(run.runId);
        }
        return false;
    }

    return writeJsonFile(path, run.toJson(), error);
}

bool ChatContextStore::updateRun(const RunRecord &run, QString *error)
{
    if (ensureOpen(error) == false)
    {
        return false;
    }
    if (run.runId.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Run is missing an identifier");
        }
        return false;
    }

    const QString path = runPath(run.runId);
    if (QFileInfo::exists(path) == false)
    {
        return false;
    }

    return writeJsonFile(path, run.toJson(), error);
}

bool ChatContextStore::appendMessage(ContextMessage *message, QString *error)
{
    if (message == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Message pointer is null");
        }
        return false;
    }
    if (ensureOpen(error) == false)
    {
        return false;
    }

    if (message->messageId.isEmpty() == true)
    {
        message->messageId = newContextId();
    }
    if (message->createdAt.isEmpty() == true)
    {
        message->createdAt = nowUtcIsoString();
    }
    if (message->tokenEstimate <= 0)
    {
        message->tokenEstimate = estimateTokenCount(message->content);
    }

    QString localError;
    ConversationRecord storedConversation = conversation(message->conversationId, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (storedConversation.isValid() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation not found: %1").arg(message->conversationId);
        }
        return false;
    }

    const int nextSequence = latestMessageSequence(message->conversationId, &localError) + 1;
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (nextSequence <= 0)
    {
        return false;
    }
    message->sequence = nextSequence;

    if (ensureDirectoryExists(conversationDirectoryPath(message->conversationId), error) == false)
    {
        return false;
    }
    if (appendJsonLine(conversationMessagesPath(message->conversationId), message->toJson(),
                       error) == false)
    {
        return false;
    }

    storedConversation.updatedAt = message->createdAt;
    storedConversation.lastMessageSequence = message->sequence;
    if ((storedConversation.title.isEmpty() == true) && (message->role == QStringLiteral("user")))
    {
        storedConversation.title = truncateForContext(message->content.simplified(), 96);
    }

    return writeJsonFile(conversationStatePath(message->conversationId),
                         storedConversation.toJson(), error);
}

bool ChatContextStore::upsertMemoryItem(MemoryItem *item, QString *error)
{
    if (item == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Memory item pointer is null");
        }
        return false;
    }
    if (ensureOpen(error) == false)
    {
        return false;
    }

    QString localError;
    const QJsonObject memoryState = readJsonFile(memoryPath(), nullptr, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    QList<MemoryItem> items = memoryItemsFromState(memoryState);

    int existingIndex = -1;
    for (int index = 0; index < items.size(); ++index)
    {
        const MemoryItem &existingItem = items.at(index);
        if ((existingItem.workspaceId == item->workspaceId) && (existingItem.key == item->key))
        {
            existingIndex = index;
            break;
        }
    }

    if (existingIndex >= 0)
    {
        item->memoryId = items.at(existingIndex).memoryId;
    }
    if (item->memoryId.isEmpty() == true)
    {
        item->memoryId = newContextId();
    }
    if (item->updatedAt.isEmpty() == true)
    {
        item->updatedAt = nowUtcIsoString();
    }
    if (item->tokenEstimate <= 0)
    {
        item->tokenEstimate = estimateTokenCount(item->value);
    }

    if (existingIndex >= 0)
    {
        items[existingIndex] = *item;
    }
    else
    {
        items.append(*item);
    }

    return writeJsonFile(memoryPath(), memoryStateToJson(item->workspaceId, items), error);
}

bool ChatContextStore::addSummary(ContextSummary *summary, QString *error)
{
    if (summary == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Summary pointer is null");
        }
        return false;
    }
    if (ensureOpen(error) == false)
    {
        return false;
    }

    if (summary->summaryId.isEmpty() == true)
    {
        summary->summaryId = newContextId();
    }
    if (summary->createdAt.isEmpty() == true)
    {
        summary->createdAt = nowUtcIsoString();
    }
    if (summary->tokenEstimate <= 0)
    {
        summary->tokenEstimate = estimateTokenCount(summary->content);
    }

    QString localError;
    const ContextSummary currentSummary = latestSummary(summary->conversationId, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if ((currentSummary.isValid() == true) && (summary->version <= currentSummary.version))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Summary version %1 is not newer than the current version %2")
                         .arg(summary->version)
                         .arg(currentSummary.version);
        }
        return false;
    }

    if (ensureDirectoryExists(conversationDirectoryPath(summary->conversationId), error) == false)
    {
        return false;
    }
    if (appendJsonLine(conversationSummariesPath(summary->conversationId), summary->toJson(),
                       error) == false)
    {
        return false;
    }

    return writeJsonFile(conversationLatestSummaryPath(summary->conversationId), summary->toJson(),
                         error);
}

bool ChatContextStore::addArtifact(ArtifactRecord *artifact, const QString &content,
                                   QString *error)
{
    if (artifact == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Artifact pointer is null");
        }
        return false;
    }
    if (ensureOpen(error) == false)
    {
        return false;
    }

    if (artifact->artifactId.isEmpty() == true)
    {
        artifact->artifactId = newContextId();
    }
    if (artifact->createdAt.isEmpty() == true)
    {
        artifact->createdAt = nowUtcIsoString();
    }
    if (artifact->preview.isEmpty() == true)
    {
        artifact->preview = truncateForContext(content, 800);
    }
    if (artifact->tokenEstimate <= 0)
    {
        artifact->tokenEstimate = estimateTokenCount(artifact->preview);
    }

    if (content.isEmpty() == false)
    {
        const QString suffix = defaultArtifactFileSuffix(artifact->kind);
        artifact->storagePath = QStringLiteral("%1%2").arg(artifact->artifactId, suffix);

        QSaveFile file(QDir(m_artifactDirPath).filePath(artifact->storagePath));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Failed to open artifact file for writing: %1")
                             .arg(file.fileName());
            }
            return false;
        }
        if (file.write(content.toUtf8()) < 0)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Failed to write artifact file: %1").arg(file.fileName());
            }
            return false;
        }
        if (file.commit() == false)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Failed to commit artifact file: %1").arg(file.fileName());
            }
            return false;
        }
    }

    if (ensureDirectoryExists(conversationDirectoryPath(artifact->conversationId), error) == false)
    {
        return false;
    }
    if (appendJsonLine(conversationArtifactsPath(artifact->conversationId), artifact->toJson(),
                       error) == false)
    {
        return false;
    }

    QString localError;
    ConversationRecord storedConversation = conversation(artifact->conversationId, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (storedConversation.isValid() == true)
    {
        storedConversation.updatedAt = artifact->createdAt;
        if (writeJsonFile(conversationStatePath(artifact->conversationId),
                          storedConversation.toJson(), error) == false)
        {
            return false;
        }
    }

    return true;
}

QList<ContextMessage> ChatContextStore::recentMessages(const QString &conversationId, int limit,
                                                       QString *error) const
{
    QList<ContextMessage> messages;
    if (ensureOpen(error) == false)
    {
        return messages;
    }

    QString localError;
    const QList<QJsonObject> objects =
        readRecentJsonLines(conversationMessagesPath(conversationId), limit, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }

    for (const QJsonObject &obj : objects)
    {
        messages.append(ContextMessage::fromJson(obj));
    }
    return messages;
}

QList<ContextMessage> ChatContextStore::messagesRange(const QString &conversationId,
                                                      int startSequenceExclusive,
                                                      int endSequenceInclusive,
                                                      QString *error) const
{
    QList<ContextMessage> messages;
    if (ensureOpen(error) == false)
    {
        return messages;
    }

    QString localError;
    const QList<QJsonObject> objects =
        readAllJsonLines(conversationMessagesPath(conversationId), &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }

    for (const QJsonObject &obj : objects)
    {
        const ContextMessage message = ContextMessage::fromJson(obj);
        if ((message.sequence > startSequenceExclusive) &&
            (message.sequence <= endSequenceInclusive))
        {
            messages.append(message);
        }
    }
    return messages;
}

QList<MemoryItem> ChatContextStore::memoryItems(const QString &workspaceId, int limit,
                                                QString *error) const
{
    QList<MemoryItem> items;
    if (ensureOpen(error) == false)
    {
        return items;
    }

    QString localError;
    items = memoryItemsFromState(readJsonFile(memoryPath(), nullptr, &localError));
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }

    QList<MemoryItem> filteredItems;
    for (const MemoryItem &item : items)
    {
        if (item.workspaceId == workspaceId)
        {
            filteredItems.append(item);
        }
    }
    items = filteredItems;

    std::sort(items.begin(), items.end(), [](const MemoryItem &left, const MemoryItem &right) {
        if (left.importance != right.importance)
        {
            return (left.importance > right.importance) ? true : false;
        }
        return (left.updatedAt > right.updatedAt) ? true : false;
    });

    if ((limit > 0) && (items.size() > limit))
    {
        items = items.mid(0, limit);
    }

    return items;
}

QList<ArtifactRecord> ChatContextStore::recentArtifacts(const QString &conversationId, int limit,
                                                        QString *error) const
{
    QList<ArtifactRecord> artifacts;
    if (ensureOpen(error) == false)
    {
        return artifacts;
    }

    QString localError;
    const QList<QJsonObject> objects =
        readRecentJsonLines(conversationArtifactsPath(conversationId), limit, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }

    for (qsizetype index = objects.size() - 1; index >= 0; --index)
    {
        artifacts.append(ArtifactRecord::fromJson(objects.at(static_cast<int>(index))));
    }
    return artifacts;
}

ContextSummary ChatContextStore::latestSummary(const QString &conversationId, QString *error) const
{
    if (ensureOpen(error) == false)
    {
        return {};
    }

    QString localError;
    const ContextSummary storedSummary =
        loadLatestSummaryState(conversationLatestSummaryPath(conversationId), &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }
    if (storedSummary.isValid() == true)
    {
        return storedSummary;
    }

    ContextSummary latest;
    const QList<QJsonObject> objects =
        readAllJsonLines(conversationSummariesPath(conversationId), &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }

    for (const QJsonObject &obj : objects)
    {
        const ContextSummary candidate = ContextSummary::fromJson(obj);
        if ((candidate.isValid() == true) &&
            ((latest.isValid() == false) || (candidate.version > latest.version)))
        {
            latest = candidate;
        }
    }

    return latest;
}

int ChatContextStore::latestSummaryVersion(const QString &conversationId, QString *error) const
{
    const ContextSummary summary = latestSummary(conversationId, error);
    if (summary.isValid() == false)
    {
        return 0;
    }
    return summary.version;
}

int ChatContextStore::latestMessageSequence(const QString &conversationId, QString *error) const
{
    if (ensureOpen(error) == false)
    {
        return -1;
    }

    int latestSequence = 0;
    QString localError;
    const QList<QJsonObject> objects =
        readAllJsonLines(conversationMessagesPath(conversationId), &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return -1;
    }

    for (const QJsonObject &obj : objects)
    {
        const int sequence = obj.value(QStringLiteral("sequence")).toInt();
        if (sequence > latestSequence)
        {
            latestSequence = sequence;
        }
    }

    return latestSequence;
}

bool ChatContextStore::initializeLayout(QString *error)
{
    if (ensureDirectoryExists(m_databasePath, error) == false)
    {
        return false;
    }
    if (ensureDirectoryExists(conversationsDirectoryPath(), error) == false)
    {
        return false;
    }
    if (ensureDirectoryExists(runsDirectoryPath(), error) == false)
    {
        return false;
    }
    if (ensureDirectoryExists(m_artifactDirPath, error) == false)
    {
        return false;
    }

    bool formatExists = false;
    QString localError;
    const QJsonObject formatObject = readJsonFile(formatPath(), &formatExists, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if ((formatExists == true) && (formatObject.isEmpty() == false))
    {
        const int version = formatObject.value(QStringLiteral("storageVersion")).toInt();
        const QString backend = formatObject.value(QStringLiteral("backend")).toString();
        if ((version != kChatContextStorageVersion) || (backend != QStringLiteral("jsonl")))
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Unsupported chat context store format in %1")
                             .arg(formatPath());
            }
            return false;
        }
    }
    else
    {
        const QJsonObject newFormat{
            {QStringLiteral("storageVersion"), kChatContextStorageVersion},
            {QStringLiteral("backend"), QStringLiteral("jsonl")},
            {QStringLiteral("layout"), QStringLiteral("conversation-directories")},
        };
        if (writeJsonFile(formatPath(), newFormat, error) == false)
        {
            return false;
        }
    }

    if (QFileInfo::exists(memoryPath()) == false)
    {
        const QJsonObject initialMemory{
            {QStringLiteral("storageVersion"), kChatContextStorageVersion},
            {QStringLiteral("workspaceId"), QStringLiteral("")},
            {QStringLiteral("items"), QJsonArray{}},
        };
        if (writeJsonFile(memoryPath(), initialMemory, error) == false)
        {
            return false;
        }
    }

    return true;
}

bool ChatContextStore::ensureOpen(QString *error) const
{
    if (isOpen() == true)
    {
        return true;
    }
    if (error != nullptr)
    {
        *error = QStringLiteral("Chat context store is not open");
    }
    return false;
}

QString ChatContextStore::conversationsDirectoryPath() const
{
    return QDir(m_databasePath).filePath(QStringLiteral("conversations"));
}

QString ChatContextStore::conversationDirectoryPath(const QString &conversationId) const
{
    return QDir(conversationsDirectoryPath()).filePath(conversationId);
}

QString ChatContextStore::conversationStatePath(const QString &conversationId) const
{
    return QDir(conversationDirectoryPath(conversationId))
        .filePath(QStringLiteral("conversation.json"));
}

QString ChatContextStore::conversationMessagesPath(const QString &conversationId) const
{
    return QDir(conversationDirectoryPath(conversationId))
        .filePath(QStringLiteral("messages.jsonl"));
}

QString ChatContextStore::conversationSummariesPath(const QString &conversationId) const
{
    return QDir(conversationDirectoryPath(conversationId))
        .filePath(QStringLiteral("summaries.jsonl"));
}

QString ChatContextStore::conversationLatestSummaryPath(const QString &conversationId) const
{
    return QDir(conversationDirectoryPath(conversationId))
        .filePath(QStringLiteral("latest-summary.json"));
}

QString ChatContextStore::conversationArtifactsPath(const QString &conversationId) const
{
    return QDir(conversationDirectoryPath(conversationId))
        .filePath(QStringLiteral("artifacts.jsonl"));
}

QString ChatContextStore::runsDirectoryPath() const
{
    return QDir(m_databasePath).filePath(QStringLiteral("runs"));
}

QString ChatContextStore::runPath(const QString &runId) const
{
    return QDir(runsDirectoryPath()).filePath(QStringLiteral("%1.json").arg(runId));
}

QString ChatContextStore::memoryPath() const
{
    return QDir(m_databasePath).filePath(QStringLiteral("memory.json"));
}

QString ChatContextStore::formatPath() const
{
    return QDir(m_databasePath).filePath(QStringLiteral("format.json"));
}

}  // namespace qcai2
