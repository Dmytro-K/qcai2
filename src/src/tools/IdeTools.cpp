#include "IdeTools.h"

#include <coreplugin/editormanager/editormanager.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QDir>
#include <QFileInfo>

namespace qcai2
{

/**
 * Returns the JSON schema for open_file_at_location arguments.
 */
QJsonObject OpenFileAtLocationTool::argsSchema() const
{
    return QJsonObject{{"path", QJsonObject{{"type", "string"}, {"required", true}}},
                       {"line", QJsonObject{{"type", "integer"}}},
                       {"column", QJsonObject{{"type", "integer"}}}};
}

/**
 * Opens a project file in Qt Creator at the requested location.
 * @param args JSON object containing the file path and optional cursor position.
 * @param workDir Project root used for sandbox validation.
 * @return A short success message or an error string.
 */
QString OpenFileAtLocationTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString relPath = args.value("path").toString();
    if (relPath.isEmpty() == true)
    {
        return QStringLiteral("Error: 'path' argument is required.");
    }

    const QString absPath = QDir(workDir).absoluteFilePath(relPath);
    if (QFileInfo::exists(absPath) == false)
    {
        return QStringLiteral("Error: file does not exist: %1").arg(absPath);
    }

    // Sandbox check
    if (((!QFileInfo(absPath).canonicalFilePath().startsWith(QDir(workDir).canonicalPath())) ==
         true))
    {
        return QStringLiteral("Error: path is outside the project directory.");
    }

    int line = args.value("line").toInt(0);
    int column = args.value("column").toInt(0);

    Utils::Link link(Utils::FilePath::fromString(absPath), line, column);
    // EditorManager::openEditorAt must be called on the main thread
    QMetaObject::invokeMethod(
        Core::EditorManager::instance(), [link]() { Core::EditorManager::openEditorAt(link); },
        Qt::QueuedConnection);

    return QStringLiteral("Opened %1 at line %2.").arg(relPath).arg(line);
}

}  // namespace qcai2
