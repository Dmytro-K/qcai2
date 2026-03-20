/*! Shared migration helpers for global settings and project-local state storage. */
#pragma once

#include <QJsonObject>
#include <QSettings>
#include <QString>

namespace qcai2::Migration
{

struct revision_t
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    bool valid = false;

    QString version_string() const;
    QString build_suffix_string() const;
    QString revision_string() const;
};

revision_t current_revision();
QString current_version_string();
QString current_build_suffix();
QString current_revision_string();

revision_t parse_revision(const QString &version, const QString &build_suffix = {});
bool is_older(const revision_t &lhs, const revision_t &rhs);

QString global_backup_dir_path();
QString global_structured_settings_file_path();
QString project_backup_dir_path(const QString &storage_path);
QString project_conversations_dir_path(const QString &storage_path);

void stamp_global_settings(QSettings &settings);
bool migrate_global_settings(QSettings &settings, QString *error = nullptr);

void stamp_project_state(QJsonObject &root);
bool migrate_project_state(const QString &storage_path, QString *error = nullptr);

QString project_goal_file_path(const QString &storage_path);
QString project_actions_log_file_path(const QString &storage_path);
QString conversation_state_file_path(const QString &storage_path, const QString &conversation_id);
QString conversation_log_journal_file_path(const QString &storage_path,
                                           const QString &conversation_id);

}  // namespace qcai2::Migration
