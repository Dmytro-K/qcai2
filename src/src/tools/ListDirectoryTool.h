#pragma once

#include "ITool.h"

#include <QDir>
#include <QStringList>

namespace qcai2
{

/**
 * Tool that lists directory contents inside the project tree.
 */
class ListDirectoryTool : public ITool
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("list_directory");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("List files and subdirectories at the given path. "
                              "Args: path (optional, defaults to project root), "
                              "depth (optional int, max recursion depth, default 2).");
    }

    /**
     * Returns the JSON schema for list_directory arguments.
     */
    QJsonObject argsSchema() const override;

    /**
     * Lists directory contents inside the sandbox.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

private:
    /**
     * Recursively collects directory entries up to the given depth.
     * @param dir Directory to list.
     * @param prefix Indentation prefix for tree display.
     * @param currentDepth Current recursion depth.
     * @param maxDepth Maximum recursion depth.
     * @param lines Output list of formatted lines.
     * @param maxEntries Hard limit on total entries collected.
     */
    void listRecursive(const QDir &dir, const QString &prefix, int currentDepth, int maxDepth,
                       QStringList &lines, int maxEntries);
};

}  // namespace qcai2
