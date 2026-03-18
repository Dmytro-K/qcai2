#pragma once

#include "ITool.h"

namespace qcai2
{

/**
 * Tool that runs an arbitrary shell command inside the project tree.
 */
class run_command_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("run_command");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Run a shell command in the project directory. "
                              "Args: command (required string, e.g. 'ls -la src/'). "
                              "The command runs with a 30-second timeout. Requires approval.");
    }

    /**
     * Returns the JSON schema for run_command arguments.
     */
    QJsonObject args_schema() const override;

    /**
     * Runs the command in the project directory.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    /**
     * Arbitrary commands require approval.
     */
    bool requires_approval() const override
    {
        return true;
    }
};

}  // namespace qcai2
