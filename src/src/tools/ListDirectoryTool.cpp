#include "ListDirectoryTool.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

namespace qcai2
{

static constexpr int kDefaultDepth = 2;
static constexpr int kMaxDepth = 5;
static constexpr int kMaxEntries = 500;

/**
 * Returns the JSON schema for list_directory arguments.
 */
QJsonObject list_directory_tool_t::args_schema() const
{
    return QJsonObject{
        {"path", QJsonObject{{"type", "string"}, {"description", "Relative path, default '.'"}}},
        {"depth", QJsonObject{{"type", "integer"}, {"description", "Max depth, default 2"}}}};
}

/**
 * Lists the directory tree inside the project sandbox.
 * @param args JSON object containing optional path and depth.
 * @param workDir Project root used for sandbox validation.
 * @return Tree-formatted listing or an error string.
 */
QString list_directory_tool_t::execute(const QJsonObject &args, const QString &workDir)
{
    const QString relPath = args.value("path").toString(QStringLiteral("."));
    int depth = args.value("depth").toInt(kDefaultDepth);
    if (depth < 1)
    {
        depth = 1;
    }
    if (depth > kMaxDepth)
    {
        depth = kMaxDepth;
    }

    const QDir root(workDir);
    const QString absPath = root.absoluteFilePath(relPath);

    // Sandbox check
    const QString canonicalPath = QFileInfo(absPath).canonicalFilePath();
    if (canonicalPath.isEmpty() == true || canonicalPath.startsWith(root.canonicalPath()) == false)
    {
        return QStringLiteral("Error: path is outside the project directory.");
    }

    const QFileInfo info(absPath);
    if (info.exists() == false)
    {
        return QStringLiteral("Error: path does not exist: %1").arg(relPath);
    }
    if (info.isDir() == false)
    {
        return QStringLiteral("Error: path is not a directory: %1").arg(relPath);
    }

    QStringList lines;
    lines.append(relPath + QStringLiteral("/"));
    this->list_recursive(QDir(absPath), QString(), 0, depth, lines, kMaxEntries);

    if (lines.size() >= kMaxEntries)
    {
        lines.append(QStringLiteral("... (truncated at %1 entries)").arg(kMaxEntries));
    }

    return lines.join('\n');
}

/**
 * Recursively lists entries, building an indented tree.
 */
void list_directory_tool_t::list_recursive(const QDir &dir, const QString &prefix,
                                           int currentDepth, int maxDepth, QStringList &lines,
                                           int maxEntries)
{
    if (currentDepth >= maxDepth || lines.size() >= maxEntries)
    {
        return;
    }

    const auto entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst);

    for (const QFileInfo &entry : entries)
    {
        if (lines.size() >= maxEntries)
        {
            return;
        }

        // Skip hidden files/dirs
        if (entry.fileName().startsWith('.'))
        {
            continue;
        }

        if (entry.isDir() == true)
        {
            lines.append(prefix + entry.fileName() + QStringLiteral("/"));
            this->list_recursive(QDir(entry.absoluteFilePath()), prefix + QStringLiteral("  "),
                                 currentDepth + 1, maxDepth, lines, maxEntries);
        }
        else
        {
            lines.append(prefix + entry.fileName());
        }
    }
}

}  // namespace qcai2
