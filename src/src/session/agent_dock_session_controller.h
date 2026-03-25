/*! Declares project-session persistence used by agent_dock_widget_t. */
#pragma once

#include <qtmcp/server_definition.h>

#include "../context/chat_context.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <memory>

class QFileSystemWatcher;
class QTimer;

namespace qcai2
{

class agent_dock_widget_t;
class chat_context_manager_t;
class i_debugger_session_service_t;

class agent_dock_session_controller_t
{
public:
    explicit agent_dock_session_controller_t(agent_dock_widget_t &dock,
                                             chat_context_manager_t *chat_context_manager,
                                             i_debugger_session_service_t *debugger_service);
    ~agent_dock_session_controller_t();

    void save_chat();
    void restore_chat();
    void refresh_project_selector();
    void switch_project_context(const QString &project_file_path);
    void switch_conversation(const QString &conversation_id);
    void start_new_conversation();
    QList<conversation_record_t> available_conversations() const;
    bool rename_conversation(const QString &conversation_id, const QString &title,
                             QString *error = nullptr);
    bool delete_conversation(const QString &conversation_id, QString *error = nullptr);

    QString current_project_file_path() const;
    QString current_project_storage_file_path() const;
    QString current_conversation_storage_file_path(const QString &conversation_id = {}) const;
    QString current_conversation_log_journal_file_path(const QString &conversation_id = {}) const;
    /** Returns the directory that stores file attachments for one conversation. */
    QString current_conversation_attachments_dir_path(const QString &conversation_id = {}) const;
    QString legacy_project_storage_file_path() const;
    QStringList current_session_watch_paths() const;
    QString current_project_dir() const;
    QString active_conversation() const;
    const qtmcp::server_definitions_t &project_mcp_servers() const;
    void append_current_conversation_log_entry(const QString &markdown_block);
    /** Resolves one stored file attachment into an absolute local file path. */
    QString absolute_path_for_attachment(const file_attachment_t &attachment) const;
    /** Copies one file into conversation-local attachment storage. */
    bool import_attachment_from_file(const QString &source_path, file_attachment_t *attachment,
                                     QString *error = nullptr);
    /** Deletes one stored attachment file from disk. */
    bool remove_attachment_file(const file_attachment_t &attachment, QString *error = nullptr);
    /** Resolves one stored image attachment into an absolute local file path. */
    QString absolute_path_for_image_attachment(const image_attachment_t &attachment) const;
    /** Copies one dropped image file into conversation-local storage. */
    bool import_image_attachment_from_file(const QString &source_path,
                                           image_attachment_t *attachment,
                                           QString *error = nullptr);
    /** Saves one pasted image into conversation-local storage. */
    bool import_image_attachment_from_image(const QImage &image, image_attachment_t *attachment,
                                            QString *error = nullptr);
    /** Deletes one stored image attachment file from disk. */
    bool remove_image_attachment_file(const image_attachment_t &attachment,
                                      QString *error = nullptr);

    void apply_project_ui_defaults();
    void migrate_legacy_session_files();
    void update_session_file_watcher();
    void reload_session_from_disk();

private:
    QString project_dir_for_path(const QString &project_file_path) const;
    void save_project_state_file();
    void save_current_conversation_state_file();
    void restore_current_conversation_state_file();
    QStringList read_current_conversation_log_blocks(bool *journal_loaded = nullptr) const;
    QString read_current_conversation_log_markdown() const;
    void write_current_conversation_log_blocks(const QStringList &markdown_blocks);
    void write_current_conversation_log_markdown(const QString &markdown);
    void mark_local_session_write();
    void refresh_conversation_list();

    agent_dock_widget_t &dock;
    chat_context_manager_t *chat_context_manager = nullptr;
    i_debugger_session_service_t *debugger_service = nullptr;
    QString active_project_file_path;
    QString active_conversation_id;
    qtmcp::server_definitions_t current_project_mcp_servers;
    std::unique_ptr<QFileSystemWatcher> session_file_watcher;
    std::unique_ptr<QTimer> session_reload_timer;
    bool session_storage_present = false;
    qint64 last_local_session_write_ms = 0;
};

}  // namespace qcai2
