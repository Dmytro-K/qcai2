#pragma once

#include "i_tool.h"

namespace qcai2
{

/**
 * Tool that reads a project file, optionally limited to a line range.
 */
class read_file_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("read_file");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral(
            "Read contents of a file. Args: path (required), start_line, end_line (optional).");
    }

    /**
     * Returns the JSON schema for file-read arguments.
     */
    QJsonObject args_schema() const override;

    /**
     * Reads the requested file contents from the sandboxed work tree.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

/**
 * Tool that applies a unified diff patch inside the project tree.
 */
class apply_patch_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("apply_patch");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral(
            "Apply a unified diff patch to files in the project. Args: diff (required string).");
    }

    /**
     * Returns the JSON schema for patch application arguments.
     */
    QJsonObject args_schema() const override;

    /**
     * Validates and applies the requested diff inside the sandbox.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    /**
     * Marks patch application as an approval-gated operation.
     */
    bool requires_approval() const override
    {
        return true;
    }
};

}  // namespace qcai2
