#pragma once

#include <QJsonObject>
#include <QString>

namespace qcai2
{

// Abstract interface for agent tools.
class ITool
{
public:
    virtual ~ITool() = default;

    // Unique tool name (e.g. "read_file")
    virtual QString name() const = 0;

    // Human-readable description for the LLM
    virtual QString description() const = 0;

    // JSON schema snippet describing accepted args
    virtual QJsonObject argsSchema() const = 0;

    // Execute the tool. Returns result text.
    // workDir is the project root; used to enforce sandbox.
    virtual QString execute(const QJsonObject &args, const QString &workDir) = 0;

    // Whether this tool needs user approval before execution
    virtual bool requiresApproval() const
    {
        return false;
    }
};

}  // namespace qcai2
