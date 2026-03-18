#pragma once

#include "i_tool.h"

namespace qcai2
{

/**
 * Tool that creates a new file inside the project tree.
 */
class create_file_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("create_file");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral(
            "Create a new file with the given content. "
            "Args: path (required, relative to project root), content (required string). "
            "Fails if the file already exists. Use apply_patch to modify existing files.");
    }

    /**
     * Returns the JSON schema for create_file arguments.
     */
    QJsonObject args_schema() const override;

    /**
     * Creates a new file with the given content inside the sandbox.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    /**
     * File creation requires approval.
     */
    bool requires_approval() const override
    {
        return true;
    }
};

}  // namespace qcai2
