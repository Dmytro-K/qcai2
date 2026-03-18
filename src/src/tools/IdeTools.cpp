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
QJsonObject open_file_at_location_tool_t::args_schema() const
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
QString open_file_at_location_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    const QString rel_path = args.value("path").toString();
    if (rel_path.isEmpty() == true)
    {
        return QStringLiteral("Error: 'path' argument is required.");
    }

    const QString abs_path = QDir(work_dir).absoluteFilePath(rel_path);
    if (QFileInfo::exists(abs_path) == false)
    {
        return QStringLiteral("Error: file does not exist: %1").arg(abs_path);
    }

    // Sandbox check
    if (((!QFileInfo(abs_path).canonicalFilePath().startsWith(QDir(work_dir).canonicalPath())) ==
         true))
    {
        return QStringLiteral("Error: path is outside the project directory.");
    }

    int line = args.value("line").toInt(0);
    int column = args.value("column").toInt(0);

    Utils::Link link(Utils::FilePath::fromString(abs_path), line, column);
    // EditorManager::openEditorAt must be called on the main thread
    QMetaObject::invokeMethod(
        Core::EditorManager::instance(), [link]() { Core::EditorManager::openEditorAt(link); },
        Qt::QueuedConnection);

    return QStringLiteral("Opened %1 at line %2.").arg(rel_path).arg(line);
}

}  // namespace qcai2
