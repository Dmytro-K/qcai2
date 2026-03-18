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
QString git_status_tool_t::execute(const QJsonObject & /*args*/, const QString &work_dir)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty() == true)
    {
        return QStringLiteral("Error: git not found on PATH.");
    }

    process_runner_t runner;
    auto res =
        runner.run(git, {QStringLiteral("status"), QStringLiteral("--porcelain")}, work_dir);

    if (((!res.success) == true))
    {
        return QStringLiteral("Error: git status failed: %1").arg(res.std_err);
    }

    return res.std_out.isEmpty() ? QStringLiteral("Working tree clean.") : res.std_out;
}

/**
 * Returns the JSON schema for optional git diff arguments.
 */
QJsonObject git_diff_tool_t::args_schema() const
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
QString git_diff_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty() == true)
    {
        return QStringLiteral("Error: git not found on PATH.");
    }

    QStringList git_args = {QStringLiteral("diff")};
    const QString path = args.value("path").toString();
    if (path.isEmpty() == false)
    {
        git_args << QStringLiteral("--") << path;
    }

    process_runner_t runner;
    auto res = runner.run(git, git_args, work_dir);

    if (((!res.success) == true))
    {
        return QStringLiteral("Error: git diff failed: %1").arg(res.std_err);
    }

    return res.std_out.isEmpty() ? QStringLiteral("No differences.") : res.std_out;
}

}  // namespace qcai2
