#include "create_file_tool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace qcai2
{

/**
 * Returns the JSON schema for create_file arguments.
 */
QJsonObject create_file_tool_t::args_schema() const
{
    return QJsonObject{{"path", QJsonObject{{"type", "string"}, {"required", true}}},
                       {"content", QJsonObject{{"type", "string"}, {"required", true}}}};
}

/**
 * Creates a new file at the given path with the provided content.
 * @param args JSON object containing path and content.
 * @param workDir Project root used for sandbox validation.
 * @return Success message or an error string.
 */
QString create_file_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    const QString rel_path = args.value("path").toString();
    if (rel_path.isEmpty() == true)
    {
        return QStringLiteral("Error: 'path' argument is required.");
    }

    const QString content = args.value("content").toString();

    const QDir root(work_dir);
    const QString abs_path = root.absoluteFilePath(rel_path);

    // Sandbox check: must stay within workDir
    const QString canonical_work = root.canonicalPath();
    const QString canonical_parent = QFileInfo(abs_path).absolutePath();
    if (canonical_parent.startsWith(canonical_work) == false)
    {
        return QStringLiteral("Error: path is outside the project directory.");
    }

    if (QFileInfo::exists(abs_path) == true)
    {
        return QStringLiteral("Error: file already exists: %1. Use apply_patch to modify it.")
            .arg(rel_path);
    }

    // Create parent directories if needed
    const QDir parent_dir = QFileInfo(abs_path).absoluteDir();
    if (parent_dir.exists() == false)
    {
        if (root.mkpath(QFileInfo(abs_path).absolutePath()) == false)
        {
            return QStringLiteral("Error: cannot create parent directories for %1.").arg(rel_path);
        }
    }

    QFile file(abs_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        return QStringLiteral("Error: cannot create file: %1").arg(file.errorString());
    }

    file.write(content.toUtf8());
    file.close();

    return QStringLiteral("Created file: %1 (%2 bytes).").arg(rel_path).arg(content.size());
}

}  // namespace qcai2
