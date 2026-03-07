#pragma once

#include "ITool.h"

namespace qcai2
{

/**
 * Read-only tool that reports git working tree status.
 */
class GitStatusTool : public ITool
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
    QJsonObject argsSchema() const override
    {
        return {};
    }

    /**
     * Runs git status in the project directory.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

};

/**
 * Read-only tool that shows git diff output for the whole tree or one path.
 */
class GitDiffTool : public ITool
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
    QJsonObject argsSchema() const override;

    /**
     * Runs git diff in the project directory.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

};

}  // namespace qcai2
