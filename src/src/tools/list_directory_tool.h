#pragma once

#include "i_tool.h"

#include <QDir>
#include <QStringList>

namespace qcai2
{

/**
 * Tool that lists directory contents inside the project tree.
 */
class list_directory_tool_t : public i_tool_t
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
    QJsonObject args_schema() const override;

    /**
     * Lists directory contents inside the sandbox.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

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
    void list_recursive(const QDir &dir, const QString &prefix, int current_depth, int max_depth,
                        QStringList &lines, int max_entries);
};

}  // namespace qcai2
