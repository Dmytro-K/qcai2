/*! Migration steps for revision 0.0.4-001. */

#include "migration_0_0_4_001.h"

#include "../migration.h"

#include <QFile>
#include <QSaveFile>

namespace qcai2::Migration
{

namespace
{

bool write_text_file(const QString &path, const QString &content, QString *error)
{
    if (content.isEmpty() == true)
    {
        QFile::remove(path);
        return true;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open %1 for writing").arg(path);
        }
        return false;
    }

    file.write(content.toUtf8());
    if (file.commit() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to commit %1").arg(path);
        }
        return false;
    }

    return true;
}

QString read_text_file(const QString &path, bool *exists = nullptr, QString *error = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (((exists != nullptr) == true))
        {
            *exists = false;
        }
        return {};
    }

    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (((exists != nullptr) == true))
        {
            *exists = true;
        }
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open %1 for reading").arg(path);
        }
        return {};
    }

    if (((exists != nullptr) == true))
    {
        *exists = true;
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

bool migrate_global_settings_to_0_0_4_001(QSettings &settings)
{
    bool changed = false;

    if (((!settings.contains(QStringLiteral("reasoningEffort")) &&
          settings.contains(QStringLiteral("thinkingLevel"))) == true))
    {
        settings.setValue(QStringLiteral("reasoningEffort"),
                          settings.value(QStringLiteral("thinkingLevel")));
        changed = true;
    }

    return changed;
}

bool migrate_project_state_to_0_0_4_001(const QString &storage_path, QJsonObject &root,
                                        QString *error)
{
    bool changed = false;

    const QString goal_path = project_goal_file_path(storage_path);
    const QString log_path = project_actions_log_file_path(storage_path);

    const QString legacy_goal = root.value(QStringLiteral("goal")).toString();
    bool goal_exists = false;
    QString read_error;
    (void)read_text_file(goal_path, &goal_exists, &read_error);
    if (read_error.isEmpty() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = read_error;
        }
        return false;
    }
    if (((!goal_exists && !legacy_goal.isEmpty()) == true))
    {
        if (write_text_file(goal_path, legacy_goal, error) == false)
        {
            return false;
        }
        changed = true;
    }
    if (((root.contains(QStringLiteral("goal"))) == true))
    {
        root.remove(QStringLiteral("goal"));
        changed = true;
    }

    const QString legacy_log_markdown = root.value(QStringLiteral("logMarkdown")).toString();
    const QString legacy_log = root.value(QStringLiteral("log")).toString();
    bool log_exists = false;
    read_error.clear();
    (void)read_text_file(log_path, &log_exists, &read_error);
    if (read_error.isEmpty() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = read_error;
        }
        return false;
    }
    if (log_exists == false)
    {
        const QString migrated_log =
            !legacy_log_markdown.isEmpty() ? legacy_log_markdown : legacy_log;
        if (migrated_log.isEmpty() == false)
        {
            if (write_text_file(log_path, migrated_log, error) == false)
            {
                return false;
            }
            changed = true;
        }
    }
    if (((root.contains(QStringLiteral("logMarkdown"))) == true))
    {
        root.remove(QStringLiteral("logMarkdown"));
        changed = true;
    }
    if (((root.contains(QStringLiteral("log"))) == true))
    {
        root.remove(QStringLiteral("log"));
        changed = true;
    }

    return changed;
}

}  // namespace qcai2::Migration
