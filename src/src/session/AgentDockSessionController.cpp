/*! Implements project-session persistence extracted from agent_dock_widget_t. */

#include "AgentDockSessionController.h"

#include "../ui/AgentDockWidget.h"

#include "../context/ChatContextManager.h"
#include "../linked_files/AgentDockLinkedFilesController.h"

#include "../settings/Settings.h"
#include "../util/Logger.h"
#include "../util/Migration.h"

#include <qtmcp/ServerDefinition.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

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

bool write_context_text_file(const QString &path, const QString &content,
                             const QString &description)
{
    if (content.isEmpty() == true)
    {
        QFile::remove(path);
        return true;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for writing: %2").arg(description, path));
        return false;
    }

    file.write(content.toUtf8());
    if (file.commit() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to commit %1: %2").arg(description, path));
        return false;
    }

    return true;
}

QString read_context_text_file(const QString &path, const QString &description, bool *ok = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (((ok != nullptr) == true))
        {
            *ok = false;
        }
        return {};
    }

    if (file.open(QIODevice::ReadOnly) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for reading: %2").arg(description, path));
        if (((ok != nullptr) == true))
        {
            *ok = false;
        }
        return {};
    }

    if (((ok != nullptr) == true))
    {
        *ok = true;
    }
    return QString::fromUtf8(file.readAll());
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
    QObject::connect(this->session_file_watcher.get(), &QFileSystemWatcher::fileChanged,
                     &this->dock, [this](const QString &) {
                         update_session_file_watcher();
                         this->session_reload_timer->start();
                     });
    QObject::connect(this->session_file_watcher.get(), &QFileSystemWatcher::directoryChanged,
                     &this->dock, [this](const QString &) {
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
    const QString goal_path = Migration::project_goal_file_path(storage_path);
    const QString log_path = Migration::project_actions_log_file_path(storage_path);

    const QString persisted_markdown = this->dock.current_log_markdown();
    const QString goal_text = this->dock.goal_edit->toPlainText();
    QStringList plan_items;
    for (int i = 0; ((i < this->dock.plan_list->count()) == true); ++i)
    {
        plan_items.append(this->dock.plan_list->item(i)->text());
    }
    const auto &cfg = settings();
    const bool ui_state_differs =
        this->dock.mode_combo->currentData().toString() != QStringLiteral("agent") ||
        this->dock.model_combo->currentText().trimmed() != cfg.model_name ||
        this->dock.reasoning_combo->currentData().toString() != cfg.reasoning_effort ||
        this->dock.thinking_combo->currentData().toString() != cfg.thinking_level ||
        this->dock.dry_run_check->isChecked() != cfg.dry_run_default;

    const bool has_content =
        !goal_text.trimmed().isEmpty() || !persisted_markdown.trimmed().isEmpty() ||
        !this->dock.current_diff.isEmpty() || !plan_items.isEmpty() ||
        !this->dock.linked_files_controller->manual_linked_files().isEmpty() ||
        !this->dock.linked_files_controller->ignored_linked_files().isEmpty() ||
        !this->current_project_mcp_servers.isEmpty() || ui_state_differs ||
        !this->active_conversation_id.isEmpty();

    if (((!has_content) == true))
    {
        QFile::remove(storage_path);
        QFile::remove(goal_path);
        QFile::remove(log_path);
        this->update_session_file_watcher();
        return;
    }

    const QFileInfo storage_info(storage_path);
    if (((!QDir().mkpath(storage_info.absolutePath())) == true))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create project context dir: %1")
                              .arg(storage_info.absolutePath()));
        return;
    }

    if (((!write_context_text_file(goal_path, goal_text, QStringLiteral("project goal file")) ||
          !write_context_text_file(log_path, persisted_markdown,
                                   QStringLiteral("project Actions Log markdown file"))) == true))
    {
        return;
    }

    QJsonObject root;
    root[QStringLiteral("diff")] = this->dock.current_diff;
    root[QStringLiteral("status")] = this->dock.status_label->text();
    root[QStringLiteral("activeTab")] = this->dock.tabs->currentIndex();
    root[QStringLiteral("mode")] = this->dock.mode_combo->currentData().toString();
    root[QStringLiteral("model")] = this->dock.model_combo->currentText().trimmed();
    root[QStringLiteral("reasoningEffort")] = this->dock.reasoning_combo->currentData().toString();
    root[QStringLiteral("thinkingLevel")] = this->dock.thinking_combo->currentData().toString();
    root[QStringLiteral("dryRun")] = this->dock.dry_run_check->isChecked();
    root[QStringLiteral("conversationId")] = this->active_conversation_id;
    root[QStringLiteral("linkedFiles")] =
        QJsonArray::fromStringList(this->dock.linked_files_controller->manual_linked_files());
    root[QStringLiteral("ignored_linked_files")] =
        QJsonArray::fromStringList(this->dock.linked_files_controller->ignored_linked_files());
    if (((!this->current_project_mcp_servers.isEmpty()) == true))
    {
        root[QStringLiteral("mcpServers")] =
            qtmcp::server_definitions_to_json(this->current_project_mcp_servers);
    }
    Migration::stamp_project_state(root);

    QJsonArray plan_json;
    for (const QString &item : plan_items)
    {
        plan_json.append(item);
    }
    root[QStringLiteral("plan")] = plan_json;

    QSaveFile file(storage_path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to open project context file for writing: %1")
                              .arg(storage_path));
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to commit project context file: %1").arg(storage_path));
    }

    this->update_session_file_watcher();
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

    const QString goal_path = Migration::project_goal_file_path(storage_path);
    const QString log_path = Migration::project_actions_log_file_path(storage_path);

    QFile file(storage_path);
    if (file.exists() == false)
    {
        return;
    }
    if (file.open(QIODevice::ReadOnly) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to open project context file for reading: %1")
                              .arg(storage_path));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Invalid project context JSON: %1").arg(storage_path));
        return;
    }

    const QJsonObject root = doc.object();
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

    {
        const QSignalBlocker mode_blocker(this->dock.mode_combo);
        const QSignalBlocker model_blocker(this->dock.model_combo);
        const QSignalBlocker reasoning_blocker(this->dock.reasoning_combo);
        const QSignalBlocker thinking_blocker(this->dock.thinking_combo);
        const QSignalBlocker dry_run_blocker(this->dock.dry_run_check);

        select_effort_value(this->dock.reasoning_combo,
                            root.value(QStringLiteral("reasoningEffort"))
                                .toString(this->dock.reasoning_combo->currentData().toString()),
                            this->dock.reasoning_combo->currentIndex());
        select_effort_value(this->dock.thinking_combo,
                            root.value(QStringLiteral("thinkingLevel"))
                                .toString(this->dock.thinking_combo->currentData().toString()),
                            this->dock.thinking_combo->currentIndex());

        const QString saved_mode = root.value(QStringLiteral("mode")).toString();
        const int mode_index = this->dock.mode_combo->findData(saved_mode);
        if (((mode_index >= 0) == true))
        {
            this->dock.mode_combo->setCurrentIndex(mode_index);
        }

        const QString saved_model = root.value(QStringLiteral("model")).toString().trimmed();
        if (saved_model.isEmpty() == false)
        {
            this->dock.model_combo->setCurrentText(saved_model);
        }

        if (((root.contains(QStringLiteral("dryRun"))) == true))
        {
            this->dock.dry_run_check->setChecked(root.value(QStringLiteral("dryRun")).toBool());
        }
    }

    bool goal_loaded_from_file = false;
    const QString goal = read_context_text_file(goal_path, QStringLiteral("project goal file"),
                                                &goal_loaded_from_file);
    const QString legacy_goal = root.value(QStringLiteral("goal")).toString();
    const QString restored_goal = goal_loaded_from_file ? goal : legacy_goal;
    if (restored_goal.isEmpty() == false)
    {
        this->dock.goal_edit->setPlainText(restored_goal);
    }

    bool log_loaded_from_file = false;
    const QString log_markdown = read_context_text_file(
        log_path, QStringLiteral("project Actions Log markdown file"), &log_loaded_from_file);
    const QString legacy_log_markdown = root.value(QStringLiteral("log_markdown")).toString();
    const QString restored_log_markdown =
        log_loaded_from_file ? log_markdown : legacy_log_markdown;
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
        if (((fence_count % 2 != 0) == true))
        {
            this->dock.log_markdown += QStringLiteral("\n```\n");
        }
        this->dock.streaming_markdown.clear();
        this->dock.render_log();
    }
    else
    {
        const QString log = root.value(QStringLiteral("log")).toString();
        if (log.isEmpty() == false)
        {
            this->dock.log_markdown = log;
            this->dock.render_log();
        }
    }

    this->dock.current_diff = root.value(QStringLiteral("diff")).toString();
    if (((!this->dock.current_diff.isEmpty()) == true))
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
        if (item.isString())
        {
            this->dock.plan_list->addItem(item.toString());
        }
    }

    QStringList manual_linked_files;
    const QJsonArray linked_files = root.value(QStringLiteral("linkedFiles")).toArray();
    for (const QJsonValue &value : linked_files)
    {
        if (value.isString())
        {
            manual_linked_files.append(value.toString());
        }
    }
    QStringList ignored_linked_files_list;
    QJsonArray ignored_linked_files = root.value(QStringLiteral("ignored_linked_files")).toArray();
    if (ignored_linked_files.isEmpty() == true)
    {
        ignored_linked_files = root.value(QStringLiteral("excludedDefaultLinkedFiles")).toArray();
    }
    for (const QJsonValue &value : ignored_linked_files)
    {
        if (value.isString())
        {
            ignored_linked_files_list.append(value.toString());
        }
    }
    this->dock.linked_files_controller->set_manual_linked_files(manual_linked_files);
    this->dock.linked_files_controller->set_ignored_linked_files(ignored_linked_files_list);
    this->dock.linked_files_controller->invalidate_candidates();
    this->dock.linked_files_controller->refresh_ui();

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

    const int tab = root.value(QStringLiteral("activeTab")).toInt(0);
    if (((tab >= 0 && tab < this->dock.tabs->count()) == true))
    {
        this->dock.tabs->setCurrentIndex(tab);
    }

    this->update_session_file_watcher();
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
    if ((this->active_conversation_id.isEmpty() == true) &&
        (this->chat_context_manager != nullptr) && (project_file_path.isEmpty() == false))
    {
        QString context_error;
        this->active_conversation_id =
            this->chat_context_manager->start_new_conversation({}, &context_error);
        if (context_error.isEmpty() == false)
        {
            QCAI_WARN(
                "Dock",
                QStringLiteral("Failed to create project conversation: %1").arg(context_error));
        }
    }
    this->update_session_file_watcher();
}

void agent_dock_session_controller_t::start_new_conversation()
{
    if (this->chat_context_manager == nullptr)
    {
        this->active_conversation_id.clear();
        return;
    }
    if (this->active_project_file_path.isEmpty() == true)
    {
        this->active_conversation_id.clear();
        return;
    }

    QString context_error;
    this->active_conversation_id =
        this->chat_context_manager->start_new_conversation({}, &context_error);
    if (context_error.isEmpty() == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to start new conversation: %1").arg(context_error));
    }
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
    const QString goal_path = Migration::project_goal_file_path(storage_path);
    const QString log_path = Migration::project_actions_log_file_path(storage_path);
    const QFileInfo storage_info(storage_path);
    const QString session_dirPath = storage_info.absolutePath();

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
    appendIfExists(goal_path);
    appendIfExists(log_path);
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
