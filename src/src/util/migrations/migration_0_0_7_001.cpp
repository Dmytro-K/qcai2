/*! Migration steps for revision 0.0.7-001. */

#include "migration_0_0_7_001.h"

#include "../migration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace qcai2::Migration
{

namespace
{

bool read_text_file_if_exists(const QString &path, QString *content, bool *exists, QString *error)
{
    if (content != nullptr)
    {
        content->clear();
    }
    if (exists != nullptr)
    {
        *exists = false;
    }

    QFile file(path);
    if (file.exists() == false)
    {
        return true;
    }

    if (exists != nullptr)
    {
        *exists = true;
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to open %1 for reading: %2").arg(path, file.errorString());
        }
        return false;
    }

    if (content != nullptr)
    {
        *content = QString::fromUtf8(file.readAll());
    }
    return true;
}

QJsonObject read_json_file_if_exists(const QString &path, QString *error)
{
    QFile file(path);
    if (file.exists() == false)
    {
        return {};
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to open %1 for reading: %2").arg(path, file.errorString());
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid JSON object in %1").arg(path);
        }
        return {};
    }
    return document.object();
}

bool write_json_file(const QString &path, const QJsonObject &root, QString *error)
{
    const QFileInfo file_info(path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to create directory for %1").arg(path);
        }
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to open %1 for writing: %2").arg(path, file.errorString());
        }
        return false;
    }

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to write %1").arg(path);
        }
        return false;
    }

    if (file.commit() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to commit %1").arg(path);
        }
        return false;
    }
    return true;
}

bool write_markdown_journal_file(const QString &path, const QString &markdown, QString *error)
{
    if (markdown.trimmed().isEmpty() == true)
    {
        QFile::remove(path);
        return true;
    }

    const QFileInfo file_info(path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to create directory for %1").arg(path);
        }
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to open %1 for writing: %2").arg(path, file.errorString());
        }
        return false;
    }

    const QJsonObject entry{{QStringLiteral("markdown"), markdown}};
    const QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to write %1").arg(path);
        }
        return false;
    }

    if (file.commit() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to commit %1").arg(path);
        }
        return false;
    }
    return true;
}

bool has_conversation_state_keys(const QJsonObject &root)
{
    static const QStringList keys = {
        QStringLiteral("diff"),
        QStringLiteral("status"),
        QStringLiteral("activeTab"),
        QStringLiteral("mode"),
        QStringLiteral("model"),
        QStringLiteral("reasoningEffort"),
        QStringLiteral("thinkingLevel"),
        QStringLiteral("dryRun"),
        QStringLiteral("linkedFiles"),
        QStringLiteral("ignored_linked_files"),
        QStringLiteral("ignoredLinkedFiles"),
        QStringLiteral("plan"),
        QStringLiteral("goal"),
        QStringLiteral("logMarkdown"),
        QStringLiteral("log_markdown"),
        QStringLiteral("log"),
    };

    for (const QString &key : keys)
    {
        if (root.contains(key) == true)
        {
            return true;
        }
    }
    return false;
}

QJsonArray normalized_string_array(const QJsonValue &value)
{
    QJsonArray normalized;
    if (value.isArray() == false)
    {
        return normalized;
    }

    for (const QJsonValue &entry : value.toArray())
    {
        if (entry.isString() == false)
        {
            continue;
        }

        const QString string_value = entry.toString().trimmed();
        if (string_value.isEmpty() == true)
        {
            continue;
        }
        normalized.append(string_value);
    }
    return normalized;
}

QJsonObject normalized_project_vector_search_object(const QJsonObject &root)
{
    QJsonObject vector_search_root;
    const QJsonValue vector_search_value = root.value(QStringLiteral("vector_search"));
    if (vector_search_value.isObject() == true)
    {
        vector_search_root = vector_search_value.toObject();
    }

    vector_search_root[QStringLiteral("exclude")] =
        normalized_string_array(vector_search_root.value(QStringLiteral("exclude")));
    return vector_search_root;
}

bool project_state_needs_vector_search_normalization(const QJsonObject &root)
{
    const QJsonValue vector_search_value = root.value(QStringLiteral("vector_search"));
    if (vector_search_value.isObject() == false)
    {
        return true;
    }

    const QJsonObject vector_search_root = vector_search_value.toObject();
    if (vector_search_root.value(QStringLiteral("exclude")).isArray() == false)
    {
        return true;
    }

    return vector_search_root != normalized_project_vector_search_object(root);
}

void copy_if_present(QJsonObject &target, const QJsonObject &source, const QString &key)
{
    const QJsonValue value = source.value(key);
    if (value.isUndefined() == false && value.isNull() == false)
    {
        target.insert(key, value);
    }
}

QString legacy_log_markdown_from_root(const QJsonObject &root)
{
    if (root.contains(QStringLiteral("logMarkdown")) == true)
    {
        return root.value(QStringLiteral("logMarkdown")).toString();
    }
    if (root.contains(QStringLiteral("log_markdown")) == true)
    {
        return root.value(QStringLiteral("log_markdown")).toString();
    }
    if (root.contains(QStringLiteral("log")) == true)
    {
        return root.value(QStringLiteral("log")).toString();
    }
    return {};
}

void remove_conversation_state_keys(QJsonObject &root)
{
    root.remove(QStringLiteral("diff"));
    root.remove(QStringLiteral("status"));
    root.remove(QStringLiteral("activeTab"));
    root.remove(QStringLiteral("mode"));
    root.remove(QStringLiteral("model"));
    root.remove(QStringLiteral("reasoningEffort"));
    root.remove(QStringLiteral("thinkingLevel"));
    root.remove(QStringLiteral("dryRun"));
    root.remove(QStringLiteral("linkedFiles"));
    root.remove(QStringLiteral("ignored_linked_files"));
    root.remove(QStringLiteral("ignoredLinkedFiles"));
    root.remove(QStringLiteral("plan"));
    root.remove(QStringLiteral("goal"));
    root.remove(QStringLiteral("logMarkdown"));
    root.remove(QStringLiteral("log_markdown"));
    root.remove(QStringLiteral("log"));
}

}  // namespace

bool project_state_needs_migration_to_0_0_7_001(const QString &storage_path,
                                                const QJsonObject &root)
{
    const bool has_embedded_state = has_conversation_state_keys(root);
    const bool goal_exists = QFileInfo::exists(project_goal_file_path(storage_path));
    const bool log_exists = QFileInfo::exists(project_actions_log_file_path(storage_path));
    return has_embedded_state || goal_exists || log_exists ||
           project_state_needs_vector_search_normalization(root);
}

bool normalize_conversation_log_storage_to_0_0_7_001(const QString &storage_path, QString *error)
{
    QDir conversations_dir(project_conversations_dir_path(storage_path));
    if (conversations_dir.exists() == false)
    {
        return false;
    }

    bool changed = false;
    const QStringList file_names =
        conversations_dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const QString &file_name : file_names)
    {
        const QString path = conversations_dir.filePath(file_name);
        QString local_error;
        QJsonObject root = read_json_file_if_exists(path, &local_error);
        if (local_error.isEmpty() == false)
        {
            if (error != nullptr)
            {
                *error = local_error;
            }
            return false;
        }
        if (root.isEmpty() == true)
        {
            continue;
        }

        const QString legacy_log_markdown = legacy_log_markdown_from_root(root);
        const bool has_legacy_log_keys = root.contains(QStringLiteral("logMarkdown")) == true ||
                                         root.contains(QStringLiteral("log_markdown")) == true ||
                                         root.contains(QStringLiteral("log")) == true;
        if (has_legacy_log_keys == false)
        {
            continue;
        }

        QString conversation_id =
            root.value(QStringLiteral("conversationId")).toString().trimmed();
        if (conversation_id.isEmpty() == true)
        {
            conversation_id = QFileInfo(file_name).completeBaseName();
        }
        if (conversation_id.isEmpty() == true)
        {
            continue;
        }

        const QString journal_path =
            conversation_log_journal_file_path(storage_path, conversation_id);
        const QFileInfo journal_info(journal_path);
        if ((journal_info.exists() == false || journal_info.size() <= 0) &&
            write_markdown_journal_file(journal_path, legacy_log_markdown, error) == false)
        {
            return false;
        }

        root.remove(QStringLiteral("logMarkdown"));
        root.remove(QStringLiteral("log_markdown"));
        root.remove(QStringLiteral("log"));
        if (write_json_file(path, root, error) == false)
        {
            return false;
        }
        changed = true;
    }

    return changed;
}

bool migrate_project_state_to_0_0_7_001(const QString &storage_path, QJsonObject &root,
                                        QString *error)
{
    QString goal_text;
    bool goal_exists = false;
    if (read_text_file_if_exists(project_goal_file_path(storage_path), &goal_text, &goal_exists,
                                 error) == false)
    {
        return false;
    }

    QString log_markdown;
    bool log_exists = false;
    if (read_text_file_if_exists(project_actions_log_file_path(storage_path), &log_markdown,
                                 &log_exists, error) == false)
    {
        return false;
    }

    const bool has_embedded_state = has_conversation_state_keys(root);
    const bool has_legacy_payload =
        project_state_needs_migration_to_0_0_7_001(storage_path, root) || has_embedded_state;

    QString conversation_id = root.value(QStringLiteral("conversationId")).toString().trimmed();
    if (conversation_id.isEmpty() == true && has_legacy_payload == true)
    {
        conversation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        root[QStringLiteral("conversationId")] = conversation_id;
    }

    if (conversation_id.isEmpty() == false && has_legacy_payload == true)
    {
        QString local_error;
        QJsonObject conversation_root = read_json_file_if_exists(
            conversation_state_file_path(storage_path, conversation_id), &local_error);
        if (local_error.isEmpty() == false)
        {
            if (error != nullptr)
            {
                *error = local_error;
            }
            return false;
        }

        conversation_root[QStringLiteral("conversationId")] = conversation_id;
        copy_if_present(conversation_root, root, QStringLiteral("diff"));
        copy_if_present(conversation_root, root, QStringLiteral("status"));
        copy_if_present(conversation_root, root, QStringLiteral("activeTab"));
        copy_if_present(conversation_root, root, QStringLiteral("mode"));
        copy_if_present(conversation_root, root, QStringLiteral("model"));
        copy_if_present(conversation_root, root, QStringLiteral("reasoningEffort"));
        copy_if_present(conversation_root, root, QStringLiteral("thinkingLevel"));
        copy_if_present(conversation_root, root, QStringLiteral("dryRun"));
        copy_if_present(conversation_root, root, QStringLiteral("linkedFiles"));
        copy_if_present(conversation_root, root, QStringLiteral("plan"));

        if (root.contains(QStringLiteral("ignored_linked_files")) == true)
        {
            conversation_root[QStringLiteral("ignored_linked_files")] =
                root.value(QStringLiteral("ignored_linked_files"));
        }
        else if (root.contains(QStringLiteral("ignoredLinkedFiles")) == true)
        {
            conversation_root[QStringLiteral("ignored_linked_files")] =
                root.value(QStringLiteral("ignoredLinkedFiles"));
        }

        if (goal_exists == true)
        {
            conversation_root[QStringLiteral("goal")] = goal_text;
        }
        else if (root.contains(QStringLiteral("goal")) == true)
        {
            conversation_root[QStringLiteral("goal")] = root.value(QStringLiteral("goal"));
        }

        QString migrated_log_markdown =
            log_exists == true ? log_markdown : legacy_log_markdown_from_root(root);

        if (write_markdown_journal_file(
                conversation_log_journal_file_path(storage_path, conversation_id),
                migrated_log_markdown, error) == false)
        {
            return false;
        }

        if (write_json_file(conversation_state_file_path(storage_path, conversation_id),
                            conversation_root, error) == false)
        {
            return false;
        }
    }

    if (goal_exists == true)
    {
        QFile::remove(project_goal_file_path(storage_path));
    }
    if (log_exists == true)
    {
        QFile::remove(project_actions_log_file_path(storage_path));
    }

    remove_conversation_state_keys(root);
    root[QStringLiteral("vector_search")] = normalized_project_vector_search_object(root);
    return true;
}

}  // namespace qcai2::Migration
