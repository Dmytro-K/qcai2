/*! @file
    @brief Implements per-launch Markdown system diagnostics logging helpers.
*/

#include "system_diagnostics_log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace qcai2
{

namespace
{

QString launch_timestamp_local()
{
    static const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    return timestamp;
}

QString launch_timestamp_for_filename()
{
    static const QString timestamp = []() {
        QString value = launch_timestamp_local();
        value.replace(QLatin1Char(':'), QLatin1Char('-'));
        return value;
    }();
    return timestamp;
}

QString logs_directory_path(const QString &workspace_root)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/logs"));
}

QString safe_text(const QString &value)
{
    return value.isEmpty() == true ? QStringLiteral("(empty)") : value;
}

QString file_header_markdown(const QString &workspace_root)
{
    QString markdown;
    markdown += QStringLiteral("# qcai2 system diagnostics log\n\n");
    markdown += QStringLiteral("- launch_local: `%1`\n").arg(launch_timestamp_local());
    markdown += QStringLiteral("- workspace_root: `%1`\n").arg(workspace_root);
    markdown += QStringLiteral("- format: Markdown append-only per launch\n");
    return markdown;
}

}  // namespace

QString system_diagnostics_log_file_path(const QString &workspace_root)
{
    if (workspace_root.isEmpty() == true)
    {
        return {};
    }

    return QDir(logs_directory_path(workspace_root))
        .filePath(QStringLiteral("system-%1.md").arg(launch_timestamp_for_filename()));
}

bool append_system_diagnostics_event(const QString &workspace_root, const QString &component,
                                     const QString &event, const QString &message,
                                     const QStringList &details, QString *error)
{
    if (workspace_root.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("System diagnostics logging requires a workspace root");
        }
        return false;
    }

    const QString directory_path = logs_directory_path(workspace_root);
    if (QDir().mkpath(directory_path) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to create system diagnostics log directory: %1")
                         .arg(directory_path);
        }
        return false;
    }

    const QString path = system_diagnostics_log_file_path(workspace_root);
    const bool new_file = QFileInfo::exists(path) == false;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open system diagnostics log file %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    if (new_file == true)
    {
        stream << file_header_markdown(workspace_root) << QStringLiteral("\n");
    }

    stream << QStringLiteral("## `%1` `%2` / `%3`\n\n")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                       safe_text(component), safe_text(event));
    if (message.isEmpty() == false)
    {
        stream << QStringLiteral("- message: %1\n").arg(message);
    }
    for (const QString &detail : details)
    {
        stream << QStringLiteral("- %1\n").arg(detail);
    }
    stream << QStringLiteral("\n");

    if (stream.status() != QTextStream::Ok)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to write system diagnostics log file %1").arg(path);
        }
        return false;
    }

    return true;
}

}  // namespace qcai2
