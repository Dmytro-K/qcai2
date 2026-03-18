#include "chat_context_store.h"

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

QJsonObject memoryStateToJson(const QString &workspace_id, const QList<memory_item_t> &items)
{
    QJsonArray array;
    for (const memory_item_t &item : items)
    {
        array.append(item.to_json());
    }

    return QJsonObject{{QStringLiteral("storageVersion"), kChatContextStorageVersion},
                       {QStringLiteral("workspace_id"), workspace_id},
                       {QStringLiteral("items"), array}};
}

QList<memory_item_t> memory_itemsFromState(const QJsonObject &obj)
{
    QList<memory_item_t> items;
    const QJsonArray array = obj.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : array)
    {
        if (value.isObject() == true)
        {
            items.append(memory_item_t::from_json(value.toObject()));
        }
    }
    return items;
}

conversation_record_t loadConversationState(const QString &path, QString *error)
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
    return conversation_record_t::from_json(obj);
}

context_summary_t loadLatestSummaryState(const QString &path, QString *error)
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
    return context_summary_t::from_json(obj);
}

}  // namespace

chat_context_store_t::chat_context_store_t() = default;

chat_context_store_t::~chat_context_store_t()
{
    close();
}

bool chat_context_store_t::open(const QString &database_path, const QString &artifact_dir_path,
                                QString *error)
{
    if ((this->is_open() == true) && (this->database_file_path == database_path) &&
        (this->artifacts_directory_path == artifact_dir_path))
    {
        return true;
    }

    this->close();

    this->database_file_path = database_path;
    this->artifacts_directory_path = artifact_dir_path;

    if (this->initialize_layout(error) == false)
    {
        this->close();
        return false;
    }

    this->open_state = true;
    return true;
}

void chat_context_store_t::close()
{
    this->database_file_path.clear();
    this->artifacts_directory_path.clear();
    this->open_state = false;
}

bool chat_context_store_t::is_open() const
{
    return this->open_state == true;
}

QString chat_context_store_t::database_path() const
{
    return this->database_file_path;
}

QString chat_context_store_t::artifact_dir_path() const
{
    return this->artifacts_directory_path;
}

bool chat_context_store_t::ensure_conversation(conversation_record_t *conversation, QString *error)
{
    if (conversation == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation pointer is null");
        }
        return false;
    }
    if (this->ensure_open(error) == false)
    {
        return false;
    }
    if (conversation->is_valid() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation is missing required identifiers");
        }
        return false;
    }

    if (conversation->created_at.isEmpty() == true)
    {
        conversation->created_at = now_utc_iso_string();
    }
    if (conversation->updated_at.isEmpty() == true)
    {
        conversation->updated_at = conversation->created_at;
    }

    if (ensureDirectoryExists(this->conversation_directory_path(conversation->conversation_id),
                              error) == false)
    {
        return false;
    }

    QString localError;
    conversation_record_t storedConversation =
        this->conversation(conversation->conversation_id, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (storedConversation.is_valid() == false)
    {
        storedConversation = *conversation;
    }

    storedConversation.workspace_id = conversation->workspace_id;
    storedConversation.workspace_root = nonNullString(conversation->workspace_root);
    storedConversation.title = nonNullString(conversation->title);
    storedConversation.updated_at = conversation->updated_at;
    storedConversation.metadata = conversation->metadata;

    if (storedConversation.created_at.isEmpty() == true)
    {
        storedConversation.created_at = conversation->created_at;
    }

    if (writeJsonFile(this->conversation_state_path(conversation->conversation_id),
                      storedConversation.to_json(), error) == false)
    {
        return false;
    }

    *conversation = this->conversation(conversation->conversation_id, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    return conversation->is_valid();
}

conversation_record_t chat_context_store_t::conversation(const QString &conversation_id,
                                                         QString *error) const
{
    if (this->ensure_open(error) == false)
    {
        return {};
    }

    return loadConversationState(this->conversation_state_path(conversation_id), error);
}

bool chat_context_store_t::save_run(const run_record_t &run, QString *error)
{
    if (this->ensure_open(error) == false)
    {
        return false;
    }
    if (run.run_id.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Run is missing an identifier");
        }
        return false;
    }

    if (ensureDirectoryExists(this->runs_directory_path(), error) == false)
    {
        return false;
    }

    const QString path = this->run_path(run.run_id);
    if (QFileInfo::exists(path) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Run already exists: %1").arg(run.run_id);
        }
        return false;
    }

    return writeJsonFile(path, run.to_json(), error);
}

bool chat_context_store_t::update_run(const run_record_t &run, QString *error)
{
    if (this->ensure_open(error) == false)
    {
        return false;
    }
    if (run.run_id.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Run is missing an identifier");
        }
        return false;
    }

    const QString path = this->run_path(run.run_id);
    if (QFileInfo::exists(path) == false)
    {
        return false;
    }

    return writeJsonFile(path, run.to_json(), error);
}

bool chat_context_store_t::append_message(context_message_t *message, QString *error)
{
    if (message == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Message pointer is null");
        }
        return false;
    }
    if (this->ensure_open(error) == false)
    {
        return false;
    }

    if (message->message_id.isEmpty() == true)
    {
        message->message_id = new_context_id();
    }
    if (message->created_at.isEmpty() == true)
    {
        message->created_at = now_utc_iso_string();
    }
    if (message->token_estimate <= 0)
    {
        message->token_estimate = estimate_token_count(message->content);
    }

    QString localError;
    conversation_record_t storedConversation =
        this->conversation(message->conversation_id, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (storedConversation.is_valid() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation not found: %1").arg(message->conversation_id);
        }
        return false;
    }

    const int nextSequence =
        this->latest_message_sequence(message->conversation_id, &localError) + 1;
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

    if (ensureDirectoryExists(this->conversation_directory_path(message->conversation_id),
                              error) == false)
    {
        return false;
    }
    if (appendJsonLine(this->conversation_messages_path(message->conversation_id),
                       message->to_json(), error) == false)
    {
        return false;
    }

    storedConversation.updated_at = message->created_at;
    storedConversation.last_message_sequence = message->sequence;
    if ((storedConversation.title.isEmpty() == true) && (message->role == QStringLiteral("user")))
    {
        storedConversation.title = truncate_for_context(message->content.simplified(), 96);
    }

    return writeJsonFile(this->conversation_state_path(message->conversation_id),
                         storedConversation.to_json(), error);
}

bool chat_context_store_t::upsert_memory_item(memory_item_t *item, QString *error)
{
    if (item == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Memory item pointer is null");
        }
        return false;
    }
    if (this->ensure_open(error) == false)
    {
        return false;
    }

    QString localError;
    const QJsonObject memoryState = readJsonFile(this->memory_path(), nullptr, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    QList<memory_item_t> items = memory_itemsFromState(memoryState);

    int existingIndex = -1;
    for (int index = 0; index < items.size(); ++index)
    {
        const memory_item_t &existingItem = items.at(index);
        if ((existingItem.workspace_id == item->workspace_id) && (existingItem.key == item->key))
        {
            existingIndex = index;
            break;
        }
    }

    if (existingIndex >= 0)
    {
        item->memory_id = items.at(existingIndex).memory_id;
    }
    if (item->memory_id.isEmpty() == true)
    {
        item->memory_id = new_context_id();
    }
    if (item->updated_at.isEmpty() == true)
    {
        item->updated_at = now_utc_iso_string();
    }
    if (item->token_estimate <= 0)
    {
        item->token_estimate = estimate_token_count(item->value);
    }

    if (existingIndex >= 0)
    {
        items[existingIndex] = *item;
    }
    else
    {
        items.append(*item);
    }

    return writeJsonFile(this->memory_path(), memoryStateToJson(item->workspace_id, items), error);
}

bool chat_context_store_t::add_summary(context_summary_t *summary, QString *error)
{
    if (summary == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Summary pointer is null");
        }
        return false;
    }
    if (this->ensure_open(error) == false)
    {
        return false;
    }

    if (summary->summary_id.isEmpty() == true)
    {
        summary->summary_id = new_context_id();
    }
    if (summary->created_at.isEmpty() == true)
    {
        summary->created_at = now_utc_iso_string();
    }
    if (summary->token_estimate <= 0)
    {
        summary->token_estimate = estimate_token_count(summary->content);
    }

    QString localError;
    const context_summary_t currentSummary =
        this->latest_summary(summary->conversation_id, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if ((currentSummary.is_valid() == true) && (summary->version <= currentSummary.version))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Summary version %1 is not newer than the current version %2")
                         .arg(summary->version)
                         .arg(currentSummary.version);
        }
        return false;
    }

    if (ensureDirectoryExists(this->conversation_directory_path(summary->conversation_id),
                              error) == false)
    {
        return false;
    }
    if (appendJsonLine(this->conversation_summaries_path(summary->conversation_id),
                       summary->to_json(), error) == false)
    {
        return false;
    }

    return writeJsonFile(this->conversation_latest_summary_path(summary->conversation_id),
                         summary->to_json(), error);
}

bool chat_context_store_t::add_artifact(artifact_record_t *artifact, const QString &content,
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
    if (this->ensure_open(error) == false)
    {
        return false;
    }

    if (artifact->artifact_id.isEmpty() == true)
    {
        artifact->artifact_id = new_context_id();
    }
    if (artifact->created_at.isEmpty() == true)
    {
        artifact->created_at = now_utc_iso_string();
    }
    if (artifact->preview.isEmpty() == true)
    {
        artifact->preview = truncate_for_context(content, 800);
    }
    if (artifact->token_estimate <= 0)
    {
        artifact->token_estimate = estimate_token_count(artifact->preview);
    }

    if (content.isEmpty() == false)
    {
        const QString suffix = default_artifact_file_suffix(artifact->kind);
        artifact->storage_path = QStringLiteral("%1%2").arg(artifact->artifact_id, suffix);

        QSaveFile file(QDir(this->artifacts_directory_path).filePath(artifact->storage_path));
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

    if (ensureDirectoryExists(this->conversation_directory_path(artifact->conversation_id),
                              error) == false)
    {
        return false;
    }
    if (appendJsonLine(this->conversation_artifacts_path(artifact->conversation_id),
                       artifact->to_json(), error) == false)
    {
        return false;
    }

    QString localError;
    conversation_record_t storedConversation =
        this->conversation(artifact->conversation_id, &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }
    if (storedConversation.is_valid() == true)
    {
        storedConversation.updated_at = artifact->created_at;
        if (writeJsonFile(this->conversation_state_path(artifact->conversation_id),
                          storedConversation.to_json(), error) == false)
        {
            return false;
        }
    }

    return true;
}

QList<context_message_t> chat_context_store_t::recent_messages(const QString &conversation_id,
                                                               int limit, QString *error) const
{
    QList<context_message_t> messages;
    if (this->ensure_open(error) == false)
    {
        return messages;
    }

    QString localError;
    const QList<QJsonObject> objects =
        readRecentJsonLines(this->conversation_messages_path(conversation_id), limit, &localError);
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
        messages.append(context_message_t::from_json(obj));
    }
    return messages;
}

QList<context_message_t> chat_context_store_t::messages_range(const QString &conversation_id,
                                                              int start_sequenceExclusive,
                                                              int end_sequenceInclusive,
                                                              QString *error) const
{
    QList<context_message_t> messages;
    if (this->ensure_open(error) == false)
    {
        return messages;
    }

    QString localError;
    const QList<QJsonObject> objects =
        readAllJsonLines(this->conversation_messages_path(conversation_id), &localError);
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
        const context_message_t message = context_message_t::from_json(obj);
        if ((message.sequence > start_sequenceExclusive) &&
            (message.sequence <= end_sequenceInclusive))
        {
            messages.append(message);
        }
    }
    return messages;
}

QList<memory_item_t> chat_context_store_t::memory_items(const QString &workspace_id, int limit,
                                                        QString *error) const
{
    QList<memory_item_t> items;
    if (this->ensure_open(error) == false)
    {
        return items;
    }

    QString localError;
    items = memory_itemsFromState(readJsonFile(this->memory_path(), nullptr, &localError));
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }

    QList<memory_item_t> filteredItems;
    for (const memory_item_t &item : items)
    {
        if (item.workspace_id == workspace_id)
        {
            filteredItems.append(item);
        }
    }
    items = filteredItems;

    std::sort(items.begin(), items.end(),
              [](const memory_item_t &left, const memory_item_t &right) {
                  if (left.importance != right.importance)
                  {
                      return (left.importance > right.importance) ? true : false;
                  }
                  return (left.updated_at > right.updated_at) ? true : false;
              });

    if ((limit > 0) && (items.size() > limit))
    {
        items = items.mid(0, limit);
    }

    return items;
}

QList<artifact_record_t> chat_context_store_t::recent_artifacts(const QString &conversation_id,
                                                                int limit, QString *error) const
{
    QList<artifact_record_t> artifacts;
    if (this->ensure_open(error) == false)
    {
        return artifacts;
    }

    QString localError;
    const QList<QJsonObject> objects = readRecentJsonLines(
        this->conversation_artifacts_path(conversation_id), limit, &localError);
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
        artifacts.append(artifact_record_t::from_json(objects.at(static_cast<int>(index))));
    }
    return artifacts;
}

context_summary_t chat_context_store_t::latest_summary(const QString &conversation_id,
                                                       QString *error) const
{
    if (this->ensure_open(error) == false)
    {
        return {};
    }

    QString localError;
    const context_summary_t storedSummary = loadLatestSummaryState(
        this->conversation_latest_summary_path(conversation_id), &localError);
    if (localError.isEmpty() == false)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return {};
    }
    if (storedSummary.is_valid() == true)
    {
        return storedSummary;
    }

    context_summary_t latest;
    const QList<QJsonObject> objects =
        readAllJsonLines(this->conversation_summaries_path(conversation_id), &localError);
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
        const context_summary_t candidate = context_summary_t::from_json(obj);
        if ((candidate.is_valid() == true) &&
            ((latest.is_valid() == false) || (candidate.version > latest.version)))
        {
            latest = candidate;
        }
    }

    return latest;
}

int chat_context_store_t::latest_summary_version(const QString &conversation_id,
                                                 QString *error) const
{
    const context_summary_t summary = this->latest_summary(conversation_id, error);
    if (summary.is_valid() == false)
    {
        return 0;
    }
    return summary.version;
}

int chat_context_store_t::latest_message_sequence(const QString &conversation_id,
                                                  QString *error) const
{
    if (this->ensure_open(error) == false)
    {
        return -1;
    }

    int latestSequence = 0;
    QString localError;
    const QList<QJsonObject> objects =
        readAllJsonLines(this->conversation_messages_path(conversation_id), &localError);
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

bool chat_context_store_t::initialize_layout(QString *error)
{
    if (ensureDirectoryExists(this->database_file_path, error) == false)
    {
        return false;
    }
    if (ensureDirectoryExists(this->conversations_directory_path(), error) == false)
    {
        return false;
    }
    if (ensureDirectoryExists(this->runs_directory_path(), error) == false)
    {
        return false;
    }
    if (ensureDirectoryExists(this->artifacts_directory_path, error) == false)
    {
        return false;
    }

    bool formatExists = false;
    QString localError;
    const QJsonObject formatObject = readJsonFile(this->format_path(), &formatExists, &localError);
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
                             .arg(this->format_path());
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
        if (writeJsonFile(this->format_path(), newFormat, error) == false)
        {
            return false;
        }
    }

    if (QFileInfo::exists(this->memory_path()) == false)
    {
        const QJsonObject initialMemory{
            {QStringLiteral("storageVersion"), kChatContextStorageVersion},
            {QStringLiteral("workspace_id"), QStringLiteral("")},
            {QStringLiteral("items"), QJsonArray{}},
        };
        if (writeJsonFile(memory_path(), initialMemory, error) == false)
        {
            return false;
        }
    }

    return true;
}

bool chat_context_store_t::ensure_open(QString *error) const
{
    if (this->is_open() == true)
    {
        return true;
    }
    if (error != nullptr)
    {
        *error = QStringLiteral("Chat context store is not open");
    }
    return false;
}

QString chat_context_store_t::conversations_directory_path() const
{
    return QDir(this->database_file_path).filePath(QStringLiteral("conversations"));
}

QString chat_context_store_t::conversation_directory_path(const QString &conversation_id) const
{
    return QDir(this->conversations_directory_path()).filePath(conversation_id);
}

QString chat_context_store_t::conversation_state_path(const QString &conversation_id) const
{
    return QDir(this->conversation_directory_path(conversation_id))
        .filePath(QStringLiteral("conversation.json"));
}

QString chat_context_store_t::conversation_messages_path(const QString &conversation_id) const
{
    return QDir(this->conversation_directory_path(conversation_id))
        .filePath(QStringLiteral("messages.jsonl"));
}

QString chat_context_store_t::conversation_summaries_path(const QString &conversation_id) const
{
    return QDir(this->conversation_directory_path(conversation_id))
        .filePath(QStringLiteral("summaries.jsonl"));
}

QString
chat_context_store_t::conversation_latest_summary_path(const QString &conversation_id) const
{
    return QDir(this->conversation_directory_path(conversation_id))
        .filePath(QStringLiteral("latest-summary.json"));
}

QString chat_context_store_t::conversation_artifacts_path(const QString &conversation_id) const
{
    return QDir(this->conversation_directory_path(conversation_id))
        .filePath(QStringLiteral("artifacts.jsonl"));
}

QString chat_context_store_t::runs_directory_path() const
{
    return QDir(this->database_file_path).filePath(QStringLiteral("runs"));
}

QString chat_context_store_t::run_path(const QString &run_id) const
{
    return QDir(this->runs_directory_path()).filePath(QStringLiteral("%1.json").arg(run_id));
}

QString chat_context_store_t::memory_path() const
{
    return QDir(this->database_file_path).filePath(QStringLiteral("memory.json"));
}

QString chat_context_store_t::format_path() const
{
    return QDir(this->database_file_path).filePath(QStringLiteral("format.json"));
}

}  // namespace qcai2
