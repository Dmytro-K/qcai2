/*! Declares project-session persistence used by agent_dock_widget_t. */
#pragma once

#include <qtmcp/server_definition.h>

#include <QString>
#include <QStringList>

#include <memory>

class QFileSystemWatcher;
class QTimer;

namespace qcai2
{

class agent_dock_widget_t;
class chat_context_manager_t;

class agent_dock_session_controller_t
{
public:
    explicit agent_dock_session_controller_t(agent_dock_widget_t &dock,
                                             chat_context_manager_t *chat_context_manager);
    ~agent_dock_session_controller_t();

    void save_chat();
    void restore_chat();
    void refresh_project_selector();
    void switch_project_context(const QString &project_file_path);
    void start_new_conversation();

    QString current_project_file_path() const;
    QString current_project_storage_file_path() const;
    QString legacy_project_storage_file_path() const;
    QStringList current_session_watch_paths() const;
    QString current_project_dir() const;
    const qtmcp::server_definitions_t &project_mcp_servers() const;

    void apply_project_ui_defaults();
    void migrate_legacy_session_files();
    void update_session_file_watcher();
    void reload_session_from_disk();

private:
    QString project_dir_for_path(const QString &project_file_path) const;

    agent_dock_widget_t &dock;
    chat_context_manager_t *chat_context_manager = nullptr;
    QString active_project_file_path;
    QString active_conversation_id;
    qtmcp::server_definitions_t current_project_mcp_servers;
    std::unique_ptr<QFileSystemWatcher> session_file_watcher;
    std::unique_ptr<QTimer> session_reload_timer;
    bool session_storage_present = false;
};

}  // namespace qcai2
