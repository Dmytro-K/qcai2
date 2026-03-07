#include "GitTools.h"
#include "../util/ProcessRunner.h"

#include <QStandardPaths>

namespace qcai2
{

// ---------------------------------------------------------------------------
// GitStatusTool
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// GitDiffTool
// ---------------------------------------------------------------------------

QJsonObject GitDiffTool::argsSchema() const
{
    return QJsonObject{
        {"path", QJsonObject{{"type", "string"}, {"description", "Optional file path to diff"}}}};
}

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
