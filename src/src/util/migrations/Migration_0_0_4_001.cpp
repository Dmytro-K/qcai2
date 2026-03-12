/*! Migration steps for revision 0.0.4-001. */

#include "Migration_0_0_4_001.h"

#include "../Migration.h"

#include <QFile>
#include <QSaveFile>

namespace qcai2::Migration
{

namespace
{

bool writeTextFile(const QString &path, const QString &content, QString *error)
{
    if (content.isEmpty())
    {
        QFile::remove(path);
        return true;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error != nullptr)
            *error = QStringLiteral("Failed to open %1 for writing").arg(path);
        return false;
    }

    file.write(content.toUtf8());
    if (!file.commit())
    {
        if (error != nullptr)
            *error = QStringLiteral("Failed to commit %1").arg(path);
        return false;
    }

    return true;
}

QString readTextFile(const QString &path, bool *exists = nullptr, QString *error = nullptr)
{
    QFile file(path);
    if (!file.exists())
    {
        if (exists != nullptr)
            *exists = false;
        return {};
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (exists != nullptr)
            *exists = true;
        if (error != nullptr)
            *error = QStringLiteral("Failed to open %1 for reading").arg(path);
        return {};
    }

    if (exists != nullptr)
        *exists = true;
    return QString::fromUtf8(file.readAll());
}

}  // namespace

bool migrateGlobalSettingsTo_0_0_4_001(QSettings &settings)
{
    bool changed = false;

    if (!settings.contains(QStringLiteral("reasoningEffort")) &&
        settings.contains(QStringLiteral("thinkingLevel")))
    {
        settings.setValue(QStringLiteral("reasoningEffort"),
                          settings.value(QStringLiteral("thinkingLevel")));
        changed = true;
    }

    return changed;
}

bool migrateProjectStateTo_0_0_4_001(const QString &storagePath, QJsonObject &root, QString *error)
{
    bool changed = false;

    const QString goalPath = projectGoalFilePath(storagePath);
    const QString logPath = projectActionsLogFilePath(storagePath);

    const QString legacyGoal = root.value(QStringLiteral("goal")).toString();
    bool goalExists = false;
    QString readError;
    (void)readTextFile(goalPath, &goalExists, &readError);
    if (!readError.isEmpty())
    {
        if (error != nullptr)
            *error = readError;
        return false;
    }
    if (!goalExists && !legacyGoal.isEmpty())
    {
        if (!writeTextFile(goalPath, legacyGoal, error))
            return false;
        changed = true;
    }
    if (root.contains(QStringLiteral("goal")))
    {
        root.remove(QStringLiteral("goal"));
        changed = true;
    }

    const QString legacyLogMarkdown = root.value(QStringLiteral("logMarkdown")).toString();
    const QString legacyLog = root.value(QStringLiteral("log")).toString();
    bool logExists = false;
    readError.clear();
    (void)readTextFile(logPath, &logExists, &readError);
    if (!readError.isEmpty())
    {
        if (error != nullptr)
            *error = readError;
        return false;
    }
    if (!logExists)
    {
        const QString migratedLog = !legacyLogMarkdown.isEmpty() ? legacyLogMarkdown : legacyLog;
        if (!migratedLog.isEmpty())
        {
            if (!writeTextFile(logPath, migratedLog, error))
                return false;
            changed = true;
        }
    }
    if (root.contains(QStringLiteral("logMarkdown")))
    {
        root.remove(QStringLiteral("logMarkdown"));
        changed = true;
    }
    if (root.contains(QStringLiteral("log")))
    {
        root.remove(QStringLiteral("log"));
        changed = true;
    }

    return changed;
}

}  // namespace qcai2::Migration
