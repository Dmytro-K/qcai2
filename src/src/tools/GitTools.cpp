#include "GitTools.h"
#include "../util/ProcessRunner.h"

#include <QStandardPaths>

namespace qcai2
{

/**
 * Runs git status --porcelain in the project directory.
 * @param args Unused.
 * @param workDir Repository root for the git command.
 * @return Porcelain status output or an error string.
 */
QString GitStatusTool::execute(const QJsonObject & /*args*/, const QString &workDir)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty())
        return QStringLiteral("Error: git not found on PATH.");

    ProcessRunner runner;
    auto res = runner.run(git, {QStringLiteral("status"), QStringLiteral("--porcelain")}, workDir);

    if (!res.success)
        return QStringLiteral("Error: git status failed: %1").arg(res.stdErr);

    return res.stdOut.isEmpty() ? QStringLiteral("Working tree clean.") : res.stdOut;
}

/**
 * Returns the JSON schema for optional git diff arguments.
 */
QJsonObject GitDiffTool::argsSchema() const
{
    return QJsonObject{
        {"path", QJsonObject{{"type", "string"}, {"description", "Optional file path to diff"}}}};
}

/**
 * Runs git diff for the whole tree or a single path.
 * @param args JSON object containing an optional path.
 * @param workDir Repository root for the git command.
 * @return Diff text or an error string.
 */
QString GitDiffTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty())
        return QStringLiteral("Error: git not found on PATH.");

    QStringList gitArgs = {QStringLiteral("diff")};
    const QString path = args.value("path").toString();
    if (!path.isEmpty())
        gitArgs << QStringLiteral("--") << path;

    ProcessRunner runner;
    auto res = runner.run(git, gitArgs, workDir);

    if (!res.success)
        return QStringLiteral("Error: git diff failed: %1").arg(res.stdErr);

    return res.stdOut.isEmpty() ? QStringLiteral("No differences.") : res.stdOut;
}

}  // namespace qcai2
