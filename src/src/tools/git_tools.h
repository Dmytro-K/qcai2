#pragma once

#include "i_tool.h"

namespace qcai2
{

/**
 * Read-only tool that reports git working tree status.
 */
class git_status_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("git_status");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Show git status (read-only). No args required.");
    }

    /**
     * Returns the empty schema for this no-argument tool.
     */
    QJsonObject args_schema() const override
    {
        return {};
    }

    /**
     * Runs git status in the project directory.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

/**
 * Read-only tool that shows git diff output for the whole tree or one path.
 */
class git_diff_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("git_diff");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Show git diff (read-only). Args: path (optional, specific file).");
    }

    /**
     * Returns the JSON schema for optional diff arguments.
     */
    QJsonObject args_schema() const override;

    /**
     * Runs git diff in the project directory.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

}  // namespace qcai2
