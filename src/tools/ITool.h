#pragma once

#include <QJsonObject>
#include <QString>

namespace qcai2
{

/**
 * Abstract interface implemented by agent tools.
 */
class ITool
{
public:
    /**
     * Destroys the tool interface.
     */
    virtual ~ITool() = default;

    /**
     * Returns the stable tool name, such as "read_file".
     */
    virtual QString name() const = 0;

    /**
     * Returns the user-facing tool description shown to the model.
     */
    virtual QString description() const = 0;

    /**
     * Returns a JSON schema fragment describing accepted arguments.
     */
    virtual QJsonObject argsSchema() const = 0;

    /**
     * Executes the tool against the current project.
     * @param args JSON arguments validated by the caller.
     * @param workDir Project root used for sandbox checks.
     * @return Tool result text for the agent conversation.
     */
    virtual QString execute(const QJsonObject &args, const QString &workDir) = 0;

    /**
     * Returns true when the tool should be approval-gated.
     */
    virtual bool requiresApproval() const
    {
        return false;
    }

};

}  // namespace qcai2
