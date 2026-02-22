#include "IdeTools.h"

#include <coreplugin/editormanager/editormanager.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QDir>
#include <QFileInfo>

namespace Qcai2 {

QJsonObject OpenFileAtLocationTool::argsSchema() const
{
    return QJsonObject{
        {"path",   QJsonObject{{"type", "string"}, {"required", true}}},
        {"line",   QJsonObject{{"type", "integer"}}},
        {"column", QJsonObject{{"type", "integer"}}}
    };
}

QString OpenFileAtLocationTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString relPath = args.value("path").toString();
    if (relPath.isEmpty())
        return QStringLiteral("Error: 'path' argument is required.");

    const QString absPath = QDir(workDir).absoluteFilePath(relPath);
    if (!QFileInfo::exists(absPath))
        return QStringLiteral("Error: file does not exist: %1").arg(absPath);

    // Sandbox check
    if (!QFileInfo(absPath).canonicalFilePath().startsWith(QDir(workDir).canonicalPath()))
        return QStringLiteral("Error: path is outside the project directory.");

    int line   = args.value("line").toInt(0);
    int column = args.value("column").toInt(0);

    Utils::Link link(Utils::FilePath::fromString(absPath), line, column);
    // EditorManager::openEditorAt must be called on the main thread
    QMetaObject::invokeMethod(Core::EditorManager::instance(), [link]() {
        Core::EditorManager::openEditorAt(link);
    }, Qt::QueuedConnection);

    return QStringLiteral("Opened %1 at line %2.").arg(relPath).arg(line);
}

} // namespace Qcai2
