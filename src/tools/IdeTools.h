#pragma once

#include "ITool.h"

namespace qcai2
{

/**
 * Tool that opens a project file in Qt Creator at a given location.
 */
class OpenFileAtLocationTool : public ITool
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("open_file_at_location");
    }
    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral(
            "Open a file in the editor at a specific line. "
            "Args: path (required), line (optional int), column (optional int).");
    }
    /**
     * Returns the JSON schema for file-open arguments.
     */
    QJsonObject argsSchema() const override;
    /**
     * Opens the requested file in the IDE on the main thread.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;
};

}  // namespace qcai2
