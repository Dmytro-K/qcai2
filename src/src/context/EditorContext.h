/*! Declares helpers that capture active editor and project state for prompts. */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace qcai2
{

class editor_context_t : public QObject
{
    Q_OBJECT
public:
    explicit editor_context_t(QObject *parent = nullptr);

    struct snapshot_t
    {
        QString file_path;
        QString selected_text;
        int cursor_line = -1;
        int cursor_column = -1;
        QStringList open_files;
        QString project_name;
        QString project_dir;
        QString project_file_path;
        QString build_dir;
        QString build_type;
        QString kit_name;
        QString compiler_name;
        QString compiler_path;
        QString target_name;
    };

    struct project_info_t
    {
        QString project_name;
        QString project_dir;
        QString project_file_path;
    };

    snapshot_t capture() const;
    QList<project_info_t> open_projects() const;
    void set_selected_project_file_path(const QString &project_file_path);
    QString selected_project_file_path() const;
    QString to_prompt_fragment() const;
    QString file_contents_fragment(int max_chars = 60000) const;

private:
    QString active_project_file_path;
};

}  // namespace qcai2
