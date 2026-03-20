/*! Implements project-session persistence extracted from agent_dock_widget_t. */

#include "agent_dock_session_controller.h"

#include "../ui/agent_dock_widget.h"

#include "../context/chat_context_manager.h"
#include "../linked_files/agent_dock_linked_files_controller.h"

#include "../settings/settings.h"
#include "../util/logger.h"
#include "../util/migration.h"

#include <qtmcp/server_definition.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>

namespace qcai2
{

namespace
{

QString startup_project_file_path()
{
    if (auto *project = ProjectExplorer::ProjectManager::startupProject();
        ((project != nullptr) == true))
    {
        return project->projectFilePath().toUrlishString();
    }
    return {};
}

QString legacy_sanitized_session_file_name(const QString &project_file_path)
{
    QString base_name = QFileInfo(project_file_path).completeBaseName();
    if (base_name.isEmpty() == true)
    {
        base_name = QStringLiteral("session");
    }

    base_name.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-])")),
                      QStringLiteral("_"));
    return QStringLiteral("%1.json").arg(base_name);
}

void select_effort_value(QComboBox *combo, const QString &value, int fallback_index)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallback_index);
}

bool write_context_json_file(const QString &path, const QJsonObject &root,
                             const QString &description)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for writing: %2").arg(description, path));
        return false;
    }

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size())
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to write %1: %2").arg(description, path));
        return false;
    }
    if (file.commit() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to commit %1: %2").arg(description, path));
        return false;
    }
    return true;
}

QJsonObject read_context_json_file(const QString &path, const QString &description,
                                   bool *ok = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        return {};
    }
    if (file.open(QIODevice::ReadOnly) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for reading: %2").arg(description, path));
        if (ok != nullptr)
        {
            *ok = false;
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Invalid JSON in %1: %2").arg(description, path));
        if (ok != nullptr)
        {
            *ok = false;
        }
        return {};
    }

    if (ok != nullptr)
    {
        *ok = true;
    }
    return document.object();
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

bool project_vector_search_has_excludes(const QJsonObject &vector_search_root)
{
    return vector_search_root.value(QStringLiteral("exclude")).toArray().isEmpty() == false;
}

QString legacy_log_markdown_from_root(const QJsonObject &root)
{
    return root.value(QStringLiteral("logMarkdown"))
        .toString(root.value(QStringLiteral("log_markdown"))
                      .toString(root.value(QStringLiteral("log")).toString()));
}

bool append_context_jsonl_object_file(const QString &path, const QJsonObject &entry,
                                      const QString &description)
{
    const QFileInfo file_info(path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create directory for %1: %2")
                              .arg(description, file_info.absolutePath()));
        return false;
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for append: %2").arg(description, path));
        return false;
    }

    const QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size())
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to append %1: %2").arg(description, path));
        return false;
    }
    return true;
}

QString read_context_markdown_journal_file(const QString &path, const QString &description,
                                           bool *ok = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        return {};
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for reading: %2").arg(description, path));
        if (ok != nullptr)
        {
            *ok = false;
        }
        return {};
    }

    QStringList blocks;
    while (file.atEnd() == false)
    {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() == true)
        {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (document.isObject() == false)
        {
            QCAI_WARN("Dock", QStringLiteral("Invalid JSONL in %1: %2").arg(description, path));
            if (ok != nullptr)
            {
                *ok = false;
            }
            return {};
        }
        const QString markdown = document.object().value(QStringLiteral("markdown")).toString();
        if (markdown.isEmpty() == false)
        {
            blocks.append(markdown);
        }
    }

    if (ok != nullptr)
    {
        *ok = true;
    }
    return blocks.join(QStringLiteral("\n\n"));
}

}  // namespace

agent_dock_session_controller_t::agent_dock_session_controller_t(
    agent_dock_widget_t &dock, chat_context_manager_t *chat_context_manager)
    : dock(dock), chat_context_manager(chat_context_manager),
      session_file_watcher(std::make_unique<QFileSystemWatcher>()),
      session_reload_timer(std::make_unique<QTimer>())
{
    this->session_reload_timer->setSingleShot(true);
    this->session_reload_timer->setInterval(150);
    QObject::connect(
        this->session_file_watcher.get(), &QFileSystemWatcher::fileChanged, &this->dock,
        [this](const QString &) {
            if ((QDateTime::currentMSecsSinceEpoch() - this->last_local_session_write_ms) < 500)
            {
                return;
            }
            update_session_file_watcher();
            this->session_reload_timer->start();
        });
    QObject::connect(
        this->session_file_watcher.get(), &QFileSystemWatcher::directoryChanged, &this->dock,
        [this](const QString &) {
            if ((QDateTime::currentMSecsSinceEpoch() - this->last_local_session_write_ms) < 500)
            {
                return;
            }
            update_session_file_watcher();
            this->session_reload_timer->start();
        });
    QObject::connect(this->session_reload_timer.get(), &QTimer::timeout, &this->dock,
                     [this]() { reload_session_from_disk(); });
}

agent_dock_session_controller_t::~agent_dock_session_controller_t() = default;

void agent_dock_session_controller_t::save_chat()
{
    this->migrate_legacy_session_files();
    const QString storage_path = this->current_project_storage_file_path();
    if (storage_path.isEmpty() == true)
    {
        return;
    }
    const QFileInfo storage_info(storage_path);
    if (((!QDir().mkpath(storage_info.absolutePath())) == true))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create project context dir: %1")
                              .arg(storage_info.absolutePath()));
        return;
    }
    this->save_project_state_file();
    this->save_current_conversation_state_file();
    this->update_session_file_watcher();
    this->refresh_conversation_list();
}

void agent_dock_session_controller_t::restore_chat()
{
    this->migrate_legacy_session_files();
    const QString storage_path = this->current_project_storage_file_path();
    if (storage_path.isEmpty() == true)
    {
        return;
    }
    QString migration_error;
    if (Migration::migrate_project_state(storage_path, &migration_error) == false)
    {
        if (migration_error.isEmpty() == false)
        {
            QCAI_WARN("Dock", migration_error);
        }
        return;
    }

    bool root_loaded = false;
    const QJsonObject root =
        read_context_json_file(storage_path, QStringLiteral("project context file"), &root_loaded);
    if (root_loaded == false)
    {
        this->refresh_conversation_list();
        return;
    }

    this->active_conversation_id =
        root.value(QStringLiteral("conversationId")).toString().trimmed();

    if ((this->chat_context_manager != nullptr) &&
        (this->active_project_file_path.isEmpty() == false))
    {
        QString context_error;
        const QString project_dir = this->current_project_dir();
        if (project_dir.isEmpty() == false)
        {
            if (this->chat_context_manager->set_active_workspace(
                    this->active_project_file_path, project_dir, this->active_conversation_id,
                    &context_error) == true)
            {
                if (this->active_conversation_id.isEmpty() == true)
                {
                    this->active_conversation_id =
                        this->chat_context_manager->start_new_conversation({}, &context_error);
                }
                else
                {
                    this->active_conversation_id =
                        this->chat_context_manager->ensure_active_conversation(&context_error);
                }
            }
        }

        if (context_error.isEmpty() == false)
        {
            QCAI_WARN("Dock", QStringLiteral("Failed to restore persistent chat context: %1")
                                  .arg(context_error));
        }
    }

    this->current_project_mcp_servers.clear();
    const QJsonValue mcp_servers_value = root.value(QStringLiteral("mcpServers"));
    if (((!mcp_servers_value.isUndefined() && !mcp_servers_value.isNull()) == true))
    {
        if (mcp_servers_value.isObject() == false)
        {
            QCAI_WARN("Dock",
                      QStringLiteral("Project session key 'mcpServers' must be an object."));
        }
        else
        {
            QString mcp_error;
            if (((!qtmcp::server_definitions_from_json(mcp_servers_value.toObject(),
                                                       &this->current_project_mcp_servers,
                                                       &mcp_error)) == true))
            {
                QCAI_WARN(
                    "Dock",
                    QStringLiteral("Failed to parse project MCP servers: %1").arg(mcp_error));
                this->current_project_mcp_servers.clear();
            }
        }
    }

    this->restore_current_conversation_state_file();
    this->update_session_file_watcher();
    this->refresh_conversation_list();
}

void agent_dock_session_controller_t::refresh_project_selector()
{
    if (((this->dock.project_combo == nullptr) == true))
    {
        return;
    }

    QString desired_project = startup_project_file_path();
    if (desired_project.isEmpty() == true)
    {
        if (auto *ctx = this->dock.controller->editor_context(); ((ctx != nullptr) == true))
        {
            desired_project = ctx->selected_project_file_path();
        }
    }
    if (desired_project.isEmpty() == true)
    {
        desired_project = this->active_project_file_path;
    }

    const QList<editor_context_t::project_info_t> projects =
        (this->dock.controller->editor_context() != nullptr)
            ? this->dock.controller->editor_context()->open_projects()
            : QList<editor_context_t::project_info_t>{};

    const QSignalBlocker blocker(this->dock.project_combo);
    this->dock.project_combo->clear();

    if (projects.isEmpty())
    {
        this->dock.project_combo->addItem(QObject::tr("No open projects"), QString());
        this->dock.project_combo->setEnabled(false);
        this->switch_project_context(QString());
        this->dock.update_run_state(this->dock.stop_btn->isEnabled());
        return;
    }

    QSet<QString> duplicate_names;
    QSet<QString> seen_names;
    for (const auto &project : projects)
    {
        if (seen_names.contains(project.project_name))
        {
            duplicate_names.insert(project.project_name);
        }
        seen_names.insert(project.project_name);
    }

    int selected_index = -1;
    for (int i = 0; i < projects.size(); ++i)
    {
        const auto &project = projects.at(i);
        QString label = project.project_name;
        if (duplicate_names.contains(project.project_name))
        {
            label = QStringLiteral("%1 — %2").arg(project.project_name,
                                                  QFileInfo(project.project_dir).fileName());
        }
        this->dock.project_combo->addItem(label, project.project_file_path);
        this->dock.project_combo->setItemData(i, project.project_dir, Qt::ToolTipRole);
        if (project.project_file_path == desired_project)
        {
            selected_index = i;
        }
    }

    if (selected_index < 0)
    {
        selected_index = 0;
    }

    this->dock.project_combo->setEnabled(true);
    this->dock.project_combo->setCurrentIndex(selected_index);

    const QString selected_project = this->dock.project_combo->currentData().toString();
    if (selected_project != this->active_project_file_path)
    {
        this->switch_project_context(selected_project);
    }
    this->dock.update_run_state(this->dock.stop_btn->isEnabled());
}

void agent_dock_session_controller_t::switch_project_context(const QString &project_file_path)
{
    if (project_file_path == this->active_project_file_path)
    {
        return;
    }

    if (!this->active_project_file_path.isEmpty())
    {
        this->save_chat();
    }

    this->dock.clear_chat_state();
    this->apply_project_ui_defaults();
    this->dock.linked_files_controller->invalidate_candidates();
    this->active_project_file_path = project_file_path;
    this->active_conversation_id.clear();
    this->current_project_mcp_servers.clear();

    if (auto *ctx = this->dock.controller->editor_context())
    {
        ctx->set_selected_project_file_path(project_file_path);
    }

    if ((this->chat_context_manager != nullptr) && (project_file_path.isEmpty() == false))
    {
        QString context_error;
        const QString project_dir = this->project_dir_for_path(project_file_path);
        if (project_dir.isEmpty() == false)
        {
            this->chat_context_manager->set_active_workspace(project_file_path, project_dir, {},
                                                             &context_error);
        }
        if (context_error.isEmpty() == false)
        {
            QCAI_WARN(
                "Dock",
                QStringLiteral("Failed to switch persistent chat context: %1").arg(context_error));
        }
    }

    this->restore_chat();
    if ((this->active_conversation_id.isEmpty() == true) && (project_file_path.isEmpty() == false))
    {
        const QList<conversation_record_t> conversations = this->available_conversations();
        if (conversations.isEmpty() == false)
        {
            this->switch_conversation(conversations.constFirst().conversation_id);
        }
        else
        {
            this->start_new_conversation();
        }
    }
    this->save_project_state_file();
    this->refresh_conversation_list();
    this->update_session_file_watcher();
}

void agent_dock_session_controller_t::switch_conversation(const QString &conversation_id)
{
    const QString trimmed_id = conversation_id.trimmed();
    if (trimmed_id.isEmpty() == true || trimmed_id == this->active_conversation_id)
    {
        return;
    }

    this->save_current_conversation_state_file();
    this->dock.clear_chat_state();
    this->apply_project_ui_defaults();
    this->active_conversation_id = trimmed_id;

    if ((this->chat_context_manager != nullptr) &&
        (this->active_project_file_path.isEmpty() == false))
    {
        QString context_error;
        const QString project_dir = this->current_project_dir();
        if (project_dir.isEmpty() == false &&
            this->chat_context_manager->set_active_workspace(
                this->active_project_file_path, project_dir, this->active_conversation_id,
                &context_error) == true)
        {
            this->active_conversation_id =
                this->chat_context_manager->ensure_active_conversation(&context_error);
        }
        if (context_error.isEmpty() == false)
        {
            QCAI_WARN("Dock",
                      QStringLiteral("Failed to switch conversation: %1").arg(context_error));
        }
    }

    this->restore_current_conversation_state_file();
    this->save_project_state_file();
    this->refresh_conversation_list();
    this->update_session_file_watcher();
}

void agent_dock_session_controller_t::start_new_conversation()
{
    if (this->chat_context_manager == nullptr)
    {
        this->active_conversation_id.clear();
        this->refresh_conversation_list();
        return;
    }
    if (this->active_project_file_path.isEmpty() == true)
    {
        this->active_conversation_id.clear();
        this->refresh_conversation_list();
        return;
    }

    this->save_current_conversation_state_file();
    QString context_error;
    this->active_conversation_id =
        this->chat_context_manager->start_new_conversation({}, &context_error);
    if (context_error.isEmpty() == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to start new conversation: %1").arg(context_error));
    }
    this->save_project_state_file();
    this->refresh_conversation_list();
}

QList<conversation_record_t> agent_dock_session_controller_t::available_conversations() const
{
    if (this->chat_context_manager == nullptr || this->active_project_file_path.isEmpty() == true)
    {
        return {};
    }

    QString error;
    const QList<conversation_record_t> conversations =
        this->chat_context_manager->conversations(&error);
    if (error.isEmpty() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to list conversations: %1").arg(error));
        return {};
    }
    return conversations;
}

bool agent_dock_session_controller_t::rename_conversation(const QString &conversation_id,
                                                          const QString &title, QString *error)
{
    if (this->chat_context_manager == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Chat context manager is not available");
        }
        return false;
    }

    const bool renamed =
        this->chat_context_manager->rename_conversation(conversation_id, title, error);
    if (renamed == true)
    {
        this->refresh_conversation_list();
        this->update_session_file_watcher();
    }
    return renamed;
}

bool agent_dock_session_controller_t::delete_conversation(const QString &conversation_id,
                                                          QString *error)
{
    const QString trimmed_id = conversation_id.trimmed();
    if (trimmed_id.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation identifier is empty");
        }
        return false;
    }
    if (this->chat_context_manager == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Chat context manager is not available");
        }
        return false;
    }

    QString fallback_conversation_id;
    const QList<conversation_record_t> conversations = this->available_conversations();
    for (const conversation_record_t &conversation : conversations)
    {
        if (conversation.conversation_id != trimmed_id)
        {
            fallback_conversation_id = conversation.conversation_id;
            break;
        }
    }

    if (this->chat_context_manager->delete_conversation(trimmed_id, error) == false)
    {
        return false;
    }

    const QString conversation_state_path =
        this->current_conversation_storage_file_path(trimmed_id);
    const QString conversation_log_path =
        this->current_conversation_log_journal_file_path(trimmed_id);
    this->mark_local_session_write();
    QFile::remove(conversation_state_path);
    QFile::remove(conversation_log_path);

    const bool deleted_active_conversation = trimmed_id == this->active_conversation_id;
    if (deleted_active_conversation == true)
    {
        this->dock.clear_chat_state();
        this->apply_project_ui_defaults();
        this->active_conversation_id.clear();
        if (fallback_conversation_id.isEmpty() == false)
        {
            this->switch_conversation(fallback_conversation_id);
            return true;
        }
        this->start_new_conversation();
        this->dock.clear_chat_state();
    }

    this->save_project_state_file();
    this->refresh_conversation_list();
    this->update_session_file_watcher();
    return true;
}

QString agent_dock_session_controller_t::current_project_file_path() const
{
    return this->active_project_file_path;
}

QString agent_dock_session_controller_t::current_project_storage_file_path() const
{
    if (this->active_project_file_path.isEmpty())
    {
        return {};
    }

    const QString project_dir = this->project_dir_for_path(this->active_project_file_path);
    if (project_dir.isEmpty())
    {
        return {};
    }

    return QDir(project_dir).filePath(QStringLiteral(".qcai2/session.json"));
}

QString agent_dock_session_controller_t::current_conversation_storage_file_path(
    const QString &conversation_id) const
{
    const QString storage_path = this->current_project_storage_file_path();
    const QString resolved_id =
        conversation_id.isEmpty() == true ? this->active_conversation_id : conversation_id;
    if (storage_path.isEmpty() == true || resolved_id.trimmed().isEmpty() == true)
    {
        return {};
    }

    return Migration::conversation_state_file_path(storage_path, resolved_id.trimmed());
}

QString agent_dock_session_controller_t::current_conversation_log_journal_file_path(
    const QString &conversation_id) const
{
    const QString storage_path = this->current_project_storage_file_path();
    const QString resolved_id =
        conversation_id.isEmpty() == true ? this->active_conversation_id : conversation_id;
    if (storage_path.isEmpty() == true || resolved_id.trimmed().isEmpty() == true)
    {
        return {};
    }

    return Migration::conversation_log_journal_file_path(storage_path, resolved_id.trimmed());
}

QString agent_dock_session_controller_t::legacy_project_storage_file_path() const
{
    if (this->active_project_file_path.isEmpty())
    {
        return {};
    }

    const QString project_dir = this->project_dir_for_path(this->active_project_file_path);
    if (project_dir.isEmpty())
    {
        return {};
    }

    return QDir(project_dir)
        .filePath(QStringLiteral(".qcai2/%1")
                      .arg(legacy_sanitized_session_file_name(this->active_project_file_path)));
}

QStringList agent_dock_session_controller_t::current_session_watch_paths() const
{
    const QString storage_path = this->current_project_storage_file_path();
    if (storage_path.isEmpty())
    {
        return {};
    }

    QStringList paths;
    const QFileInfo storage_info(storage_path);
    const QString session_dirPath = storage_info.absolutePath();
    const QString conversations_dir = Migration::project_conversations_dir_path(storage_path);
    const QString conversation_path = this->current_conversation_storage_file_path();
    const QString conversation_log_path = this->current_conversation_log_journal_file_path();
    const QString chat_context_conversations_dir =
        QDir(this->current_project_dir())
            .filePath(QStringLiteral(".qcai2/chat-context/conversations"));

    const auto appendIfExists = [&paths](const QString &path) {
        if (!path.isEmpty() && QFileInfo::exists(path) && !paths.contains(path))
        {
            paths.append(path);
        }
    };

    if (QDir(session_dirPath).exists())
    {
        paths.append(session_dirPath);
    }
    else
    {
        const QString project_dir = this->current_project_dir();
        if (!project_dir.isEmpty())
        {
            paths.append(project_dir);
        }
    }

    appendIfExists(storage_path);
    appendIfExists(conversations_dir);
    appendIfExists(conversation_path);
    appendIfExists(conversation_log_path);
    appendIfExists(chat_context_conversations_dir);
    return paths;
}

QString agent_dock_session_controller_t::current_project_dir() const
{
    return this->project_dir_for_path(this->active_project_file_path);
}

const qtmcp::server_definitions_t &agent_dock_session_controller_t::project_mcp_servers() const
{
    return this->current_project_mcp_servers;
}

QString agent_dock_session_controller_t::active_conversation() const
{
    return this->active_conversation_id;
}

void agent_dock_session_controller_t::save_project_state_file()
{
    const QString storage_path = this->current_project_storage_file_path();
    if (storage_path.isEmpty() == true)
    {
        return;
    }

    bool existing_root_loaded = false;
    const QJsonObject existing_root = read_context_json_file(
        storage_path, QStringLiteral("project context file"), &existing_root_loaded);
    const QJsonObject vector_search_root = normalized_project_vector_search_object(
        existing_root_loaded == true ? existing_root : QJsonObject{});

    if (this->active_conversation_id.isEmpty() == true &&
        this->current_project_mcp_servers.isEmpty() == true &&
        project_vector_search_has_excludes(vector_search_root) == false)
    {
        QFile::remove(storage_path);
        return;
    }

    QJsonObject root;
    root[QStringLiteral("conversationId")] = this->active_conversation_id;
    if (this->current_project_mcp_servers.isEmpty() == false)
    {
        root[QStringLiteral("mcpServers")] =
            qtmcp::server_definitions_to_json(this->current_project_mcp_servers);
    }
    root[QStringLiteral("vector_search")] = vector_search_root;
    Migration::stamp_project_state(root);
    this->mark_local_session_write();
    write_context_json_file(storage_path, root, QStringLiteral("project context file"));
}

void agent_dock_session_controller_t::save_current_conversation_state_file()
{
    const QString conversation_path = this->current_conversation_storage_file_path();
    const QString conversation_log_path = this->current_conversation_log_journal_file_path();
    if (conversation_path.isEmpty() == true)
    {
        return;
    }

    QStringList plan_items;
    for (int i = 0; i < this->dock.plan_list->count(); ++i)
    {
        plan_items.append(this->dock.plan_list->item(i)->text());
    }

    QJsonArray plan_json;
    for (const QString &item : plan_items)
    {
        plan_json.append(item);
    }

    const QFileInfo conversation_info(conversation_path);
    if (QDir().mkpath(conversation_info.absolutePath()) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create conversation state dir: %1")
                              .arg(conversation_info.absolutePath()));
        return;
    }

    QJsonObject root;
    root[QStringLiteral("conversationId")] = this->active_conversation_id;
    root[QStringLiteral("goal")] = this->dock.goal_edit->toPlainText();
    root[QStringLiteral("diff")] = this->dock.current_diff;
    root[QStringLiteral("status")] = this->dock.status_label->text();
    root[QStringLiteral("activeTab")] = this->dock.tabs->currentIndex();
    root[QStringLiteral("mode")] = this->dock.mode_combo->currentData().toString();
    root[QStringLiteral("model")] = this->dock.model_combo->currentText().trimmed();
    root[QStringLiteral("reasoningEffort")] = this->dock.reasoning_combo->currentData().toString();
    root[QStringLiteral("thinkingLevel")] = this->dock.thinking_combo->currentData().toString();
    root[QStringLiteral("dryRun")] = this->dock.dry_run_check->isChecked();
    root[QStringLiteral("linkedFiles")] =
        QJsonArray::fromStringList(this->dock.linked_files_controller->manual_linked_files());
    root[QStringLiteral("ignored_linked_files")] =
        QJsonArray::fromStringList(this->dock.linked_files_controller->ignored_linked_files());
    root[QStringLiteral("plan")] = plan_json;

    const QString current_log_markdown = this->dock.current_log_markdown();
    if (current_log_markdown.trimmed().isEmpty() == true)
    {
        this->mark_local_session_write();
        QFile::remove(conversation_log_path);
    }
    else if (QFileInfo::exists(conversation_log_path) == false)
    {
        this->mark_local_session_write();
        this->write_current_conversation_log_markdown(current_log_markdown);
    }

    this->mark_local_session_write();
    write_context_json_file(conversation_path, root, QStringLiteral("conversation state file"));
}

void agent_dock_session_controller_t::restore_current_conversation_state_file()
{
    const QString conversation_path = this->current_conversation_storage_file_path();
    if (conversation_path.isEmpty() == true)
    {
        return;
    }

    bool conversation_loaded = false;
    const QJsonObject root = read_context_json_file(
        conversation_path, QStringLiteral("conversation state file"), &conversation_loaded);
    if (conversation_loaded == true)
    {
        {
            const QSignalBlocker mode_blocker(this->dock.mode_combo);
            const QSignalBlocker model_blocker(this->dock.model_combo);
            const QSignalBlocker reasoning_blocker(this->dock.reasoning_combo);
            const QSignalBlocker thinking_blocker(this->dock.thinking_combo);
            const QSignalBlocker dry_run_blocker(this->dock.dry_run_check);

            select_effort_value(
                this->dock.reasoning_combo,
                root.value(QStringLiteral("reasoningEffort"))
                    .toString(this->dock.reasoning_combo->currentData().toString()),
                this->dock.reasoning_combo->currentIndex());
            select_effort_value(this->dock.thinking_combo,
                                root.value(QStringLiteral("thinkingLevel"))
                                    .toString(this->dock.thinking_combo->currentData().toString()),
                                this->dock.thinking_combo->currentIndex());

            const QString saved_mode = root.value(QStringLiteral("mode")).toString();
            const int mode_index = this->dock.mode_combo->findData(saved_mode);
            if (mode_index >= 0)
            {
                this->dock.mode_combo->setCurrentIndex(mode_index);
            }

            const QString saved_model = root.value(QStringLiteral("model")).toString().trimmed();
            if (saved_model.isEmpty() == false)
            {
                this->dock.model_combo->setCurrentText(saved_model);
            }

            if (root.contains(QStringLiteral("dryRun")) == true)
            {
                this->dock.dry_run_check->setChecked(
                    root.value(QStringLiteral("dryRun")).toBool());
            }
        }
        const QString restored_goal = root.value(QStringLiteral("goal")).toString();
        if (restored_goal.isEmpty() == false)
        {
            this->dock.goal_edit->setPlainText(restored_goal);
        }
    }

    const QString restored_log_markdown = this->read_current_conversation_log_markdown();
    if (restored_log_markdown.isEmpty() == false)
    {
        this->dock.log_markdown = restored_log_markdown;
        int fence_count = 0;
        const auto lines = this->dock.log_markdown.split('\n');
        for (const auto &line : lines)
        {
            if (line.trimmed().startsWith(QStringLiteral("```")))
            {
                ++fence_count;
            }
        }
        if ((fence_count % 2) != 0)
        {
            this->dock.log_markdown += QStringLiteral("\n```\n");
        }
        this->dock.streaming_markdown.clear();
        this->dock.render_log();
    }

    const QString legacy_log_markdown =
        conversation_loaded == true ? legacy_log_markdown_from_root(root) : QString();
    if (legacy_log_markdown.isEmpty() == false)
    {
        QJsonObject rewritten_root = root;
        rewritten_root.remove(QStringLiteral("logMarkdown"));
        rewritten_root.remove(QStringLiteral("log_markdown"));
        rewritten_root.remove(QStringLiteral("log"));
        this->write_current_conversation_log_markdown(legacy_log_markdown);
        this->mark_local_session_write();
        write_context_json_file(conversation_path, rewritten_root,
                                QStringLiteral("conversation state file"));
    }

    if (conversation_loaded == true)
    {
        this->dock.current_diff = root.value(QStringLiteral("diff")).toString();
        if (this->dock.current_diff.isEmpty() == false)
        {
            this->dock.sync_diff_ui(this->dock.current_diff, false, true);
        }

        const QString status = root.value(QStringLiteral("status")).toString();
        if (status.isEmpty() == false)
        {
            this->dock.status_label->setText(status);
        }

        const QJsonArray plan_items = root.value(QStringLiteral("plan")).toArray();
        for (const auto &item : plan_items)
        {
            if (item.isString() == true)
            {
                this->dock.plan_list->addItem(item.toString());
            }
        }

        QStringList manual_linked_files;
        const QJsonArray linked_files = root.value(QStringLiteral("linkedFiles")).toArray();
        for (const QJsonValue &value : linked_files)
        {
            if (value.isString() == true)
            {
                manual_linked_files.append(value.toString());
            }
        }
        QStringList ignored_linked_files_list;
        const QJsonArray ignored_linked_files =
            root.value(QStringLiteral("ignored_linked_files")).toArray();
        for (const QJsonValue &value : ignored_linked_files)
        {
            if (value.isString() == true)
            {
                ignored_linked_files_list.append(value.toString());
            }
        }
        this->dock.linked_files_controller->set_manual_linked_files(manual_linked_files);
        this->dock.linked_files_controller->set_ignored_linked_files(ignored_linked_files_list);
        this->dock.linked_files_controller->invalidate_candidates();
        this->dock.linked_files_controller->refresh_ui();

        const int tab = root.value(QStringLiteral("activeTab")).toInt(0);
        if (tab >= 0 && tab < this->dock.tabs->count())
        {
            this->dock.tabs->setCurrentIndex(tab);
        }
    }
}

QString agent_dock_session_controller_t::read_current_conversation_log_markdown() const
{
    const QString journal_path = this->current_conversation_log_journal_file_path();
    bool journal_loaded = false;
    const QString journal_markdown = read_context_markdown_journal_file(
        journal_path, QStringLiteral("conversation log journal file"), &journal_loaded);
    if (journal_loaded == true)
    {
        return journal_markdown;
    }

    bool conversation_loaded = false;
    const QJsonObject root =
        read_context_json_file(this->current_conversation_storage_file_path(),
                               QStringLiteral("conversation state file"), &conversation_loaded);
    if (conversation_loaded == false)
    {
        return {};
    }
    return legacy_log_markdown_from_root(root);
}

void agent_dock_session_controller_t::write_current_conversation_log_markdown(
    const QString &markdown)
{
    const QString journal_path = this->current_conversation_log_journal_file_path();
    if (journal_path.isEmpty() == true)
    {
        return;
    }

    if (markdown.trimmed().isEmpty() == true)
    {
        this->mark_local_session_write();
        QFile::remove(journal_path);
        return;
    }

    const QFileInfo file_info(journal_path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create conversation log dir: %1")
                              .arg(file_info.absolutePath()));
        return;
    }

    QSaveFile file(journal_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to open conversation log journal for writing: %1")
                              .arg(journal_path));
        return;
    }

    const QByteArray line = QJsonDocument(QJsonObject{{QStringLiteral("markdown"), markdown}})
                                .toJson(QJsonDocument::Compact) +
                            '\n';
    this->mark_local_session_write();
    if (file.write(line) != line.size() || file.commit() == false)
    {
        QCAI_WARN(
            "Dock",
            QStringLiteral("Failed to write conversation log journal: %1").arg(journal_path));
    }
}

void agent_dock_session_controller_t::append_current_conversation_log_entry(
    const QString &markdown_block)
{
    const QString trimmed = markdown_block.trimmed();
    if (trimmed.isEmpty() == true)
    {
        return;
    }

    const QString journal_path = this->current_conversation_log_journal_file_path();
    if (journal_path.isEmpty() == true)
    {
        return;
    }

    this->mark_local_session_write();
    append_context_jsonl_object_file(journal_path,
                                     QJsonObject{{QStringLiteral("markdown"), markdown_block}},
                                     QStringLiteral("conversation log journal file"));
    this->update_session_file_watcher();
}

void agent_dock_session_controller_t::mark_local_session_write()
{
    this->last_local_session_write_ms = QDateTime::currentMSecsSinceEpoch();
}

void agent_dock_session_controller_t::refresh_conversation_list()
{
    this->dock.refresh_conversation_list();
}

void agent_dock_session_controller_t::apply_project_ui_defaults()
{
    const auto &cfg = settings();
    const QSignalBlocker mode_blocker(this->dock.mode_combo);
    const QSignalBlocker model_blocker(this->dock.model_combo);
    const QSignalBlocker reasoning_blocker(this->dock.reasoning_combo);
    const QSignalBlocker thinking_blocker(this->dock.thinking_combo);
    const QSignalBlocker dry_run_blocker(this->dock.dry_run_check);

    const int mode_index = this->dock.mode_combo->findData(QStringLiteral("agent"));
    this->dock.mode_combo->setCurrentIndex(mode_index >= 0 ? mode_index : 0);
    this->dock.model_combo->setCurrentText(cfg.model_name);
    select_effort_value(this->dock.reasoning_combo, cfg.reasoning_effort, 2);
    select_effort_value(this->dock.thinking_combo, cfg.thinking_level, 2);
    this->dock.dry_run_check->setChecked(cfg.dry_run_default);
}

void agent_dock_session_controller_t::migrate_legacy_session_files()
{
    const QString storage_path = this->current_project_storage_file_path();
    const QString legacy_storage_path = this->legacy_project_storage_file_path();
    if (storage_path.isEmpty() || legacy_storage_path.isEmpty() ||
        storage_path == legacy_storage_path)
    {
        return;
    }

    const QString goal_path = Migration::project_goal_file_path(storage_path);
    const QString log_path = Migration::project_actions_log_file_path(storage_path);
    const QString legacy_goal_path = Migration::project_goal_file_path(legacy_storage_path);
    const QString legacy_log_path = Migration::project_actions_log_file_path(legacy_storage_path);

    const bool has_current_files = QFileInfo::exists(storage_path) ||
                                   QFileInfo::exists(goal_path) || QFileInfo::exists(log_path);
    const bool has_legacy_files = QFileInfo::exists(legacy_storage_path) ||
                                  QFileInfo::exists(legacy_goal_path) ||
                                  QFileInfo::exists(legacy_log_path);
    if (has_current_files || !has_legacy_files)
    {
        return;
    }

    const QFileInfo storage_info(storage_path);
    if (!QDir().mkpath(storage_info.absolutePath()))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create project context dir: %1")
                              .arg(storage_info.absolutePath()));
        return;
    }

    const auto rename_if_exists = [](const QString &from, const QString &to,
                                     const QString &label) {
        if (!QFileInfo::exists(from))
        {
            return true;
        }
        if (QFile::exists(to))
        {
            return false;
        }
        if (QFile::rename(from, to))
        {
            return true;
        }

        QCAI_WARN("Dock",
                  QStringLiteral("Failed to rename %1 from %2 to %3").arg(label, from, to));
        return false;
    };

    const bool moved_goal =
        rename_if_exists(legacy_goal_path, goal_path, QStringLiteral("legacy project goal file"));
    const bool moved_log = rename_if_exists(legacy_log_path, log_path,
                                            QStringLiteral("legacy project Actions Log file"));
    const bool moved_storage = rename_if_exists(legacy_storage_path, storage_path,
                                                QStringLiteral("legacy project context file"));

    if (!(moved_goal && moved_log && moved_storage))
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to fully migrate legacy session file names for %1")
                      .arg(this->active_project_file_path));
    }
}

void agent_dock_session_controller_t::update_session_file_watcher()
{
    const QString storage_path = this->current_project_storage_file_path();
    const QString goal_path =
        storage_path.isEmpty() ? QString() : Migration::project_goal_file_path(storage_path);
    const QString log_path = storage_path.isEmpty()
                                 ? QString()
                                 : Migration::project_actions_log_file_path(storage_path);
    const QFileInfo storage_info(storage_path);
    this->session_storage_present =
        !storage_path.isEmpty() &&
        (QFileInfo::exists(storage_path) || QFileInfo::exists(goal_path) ||
         QFileInfo::exists(log_path) || QDir(storage_info.absolutePath()).exists());

    const QStringList current_paths =
        this->session_file_watcher->files() + this->session_file_watcher->directories();
    if (!current_paths.isEmpty())
    {
        this->session_file_watcher->removePaths(current_paths);
    }

    const QStringList watch_paths = this->current_session_watch_paths();
    if (!watch_paths.isEmpty())
    {
        this->session_file_watcher->addPaths(watch_paths);
    }
}

void agent_dock_session_controller_t::reload_session_from_disk()
{
    const QString storage_path = this->current_project_storage_file_path();
    if (storage_path.isEmpty())
    {
        return;
    }

    const QString goal_path = Migration::project_goal_file_path(storage_path);
    const QString log_path = Migration::project_actions_log_file_path(storage_path);
    const QFileInfo storage_info(storage_path);
    const bool session_exists = QFileInfo::exists(storage_path) || QFileInfo::exists(goal_path) ||
                                QFileInfo::exists(log_path) ||
                                QDir(storage_info.absolutePath()).exists();
    if (!session_exists && !this->session_storage_present)
    {
        this->update_session_file_watcher();
        return;
    }

    const bool was_running = this->dock.stop_btn->isEnabled();
    this->dock.clear_chat_state();
    this->apply_project_ui_defaults();
    this->current_project_mcp_servers.clear();
    this->restore_chat();
    this->update_session_file_watcher();
    this->dock.update_run_state(was_running);
}

QString
agent_dock_session_controller_t::project_dir_for_path(const QString &project_file_path) const
{
    if (project_file_path.isEmpty())
    {
        return {};
    }

    if (auto *ctx = this->dock.controller->editor_context())
    {
        const auto projects = ctx->open_projects();
        for (const auto &project : projects)
        {
            if (project.project_file_path == project_file_path)
            {
                return project.project_dir;
            }
        }
    }

    return QFileInfo(project_file_path).absolutePath();
}

}  // namespace qcai2
