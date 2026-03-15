#include "CreateFileTool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace qcai2
{

/**
 * Returns the JSON schema for create_file arguments.
 */
QJsonObject CreateFileTool::argsSchema() const
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
QString CreateFileTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString relPath = args.value("path").toString();
    if (relPath.isEmpty() == true)
    {
        return QStringLiteral("Error: 'path' argument is required.");
    }

    const QString content = args.value("content").toString();

    const QDir root(workDir);
    const QString absPath = root.absoluteFilePath(relPath);

    // Sandbox check: must stay within workDir
    const QString canonicalWork = root.canonicalPath();
    const QString canonicalParent = QFileInfo(absPath).absolutePath();
    if (canonicalParent.startsWith(canonicalWork) == false)
    {
        return QStringLiteral("Error: path is outside the project directory.");
    }

    if (QFileInfo::exists(absPath) == true)
    {
        return QStringLiteral("Error: file already exists: %1. Use apply_patch to modify it.")
            .arg(relPath);
    }

    // Create parent directories if needed
    const QDir parentDir = QFileInfo(absPath).absoluteDir();
    if (parentDir.exists() == false)
    {
        if (root.mkpath(QFileInfo(absPath).absolutePath()) == false)
        {
            return QStringLiteral("Error: cannot create parent directories for %1.").arg(relPath);
        }
    }

    QFile file(absPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        return QStringLiteral("Error: cannot create file: %1").arg(file.errorString());
    }

    file.write(content.toUtf8());
    file.close();

    return QStringLiteral("Created file: %1 (%2 bytes).").arg(relPath).arg(content.size());
}

}  // namespace qcai2
