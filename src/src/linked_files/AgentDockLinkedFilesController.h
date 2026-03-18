/*! Declares linked-files behavior used by agent_dock_widget_t. */
#pragma once

#include <QStringList>

namespace qcai2
{

class agent_dock_widget_t;

class agent_dock_linked_files_controller_t
{
public:
    explicit agent_dock_linked_files_controller_t(agent_dock_widget_t &dock);

    QStringList linked_file_candidates() const;
    QStringList effective_linked_files() const;
    QString linked_files_prompt_context() const;
    QString linked_file_absolute_path(const QString &path) const;

    void add_linked_files(const QStringList &paths);
    void remove_selected_linked_files();
    void refresh_ui();
    void clear_state();

    const QStringList &manual_linked_files() const;
    const QStringList &ignored_linked_files() const;
    void set_manual_linked_files(const QStringList &paths);
    void set_ignored_linked_files(const QStringList &paths);
    void invalidate_candidates();

private:
    QString current_project_dir() const;
    QStringList linked_files_from_goal_text() const;
    QString current_editor_linked_file() const;
    QString default_linked_file() const;
    QString normalize_linked_file_path(const QString &path) const;

    agent_dock_widget_t &dock;
    QStringList manual_linked_file_paths;
    QStringList ignored_linked_file_paths;
    QStringList hidden_default_linked_files;
    mutable QString cached_linked_file_root;
    mutable QStringList cached_linked_file_candidates;
};

}  // namespace qcai2
