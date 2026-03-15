#include "RunCommandTool.h"
#include "../util/ProcessRunner.h"

#include <QJsonObject>

namespace qcai2
{

static constexpr int kTimeoutMs = 30000;

/**
 * Returns the JSON schema for run_command arguments.
 */
QJsonObject RunCommandTool::argsSchema() const
{
    return QJsonObject{{"command", QJsonObject{{"type", "string"}, {"required", true}}}};
}

/**
 * Runs a shell command inside the project directory.
 * @param args JSON object containing the command string.
 * @param workDir Project root used as the working directory.
 * @return Combined stdout/stderr or an error string.
 */
QString RunCommandTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString command = args.value("command").toString();
    if (command.isEmpty() == true)
    {
        return QStringLiteral("Error: 'command' argument is required.");
    }

    ProcessRunner runner;
#ifdef Q_OS_WIN
    auto res = runner.run(QStringLiteral("cmd.exe"), {QStringLiteral("/C"), command}, workDir,
                          kTimeoutMs);
#else
    auto res = runner.run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command}, workDir,
                          kTimeoutMs);
#endif

    QString output;
    if (res.stdOut.isEmpty() == false)
    {
        output += res.stdOut;
    }
    if (res.stdErr.isEmpty() == false)
    {
        if (output.isEmpty() == false)
        {
            output += QStringLiteral("\n");
        }
        output += QStringLiteral("[stderr]\n") + res.stdErr;
    }

    if (res.success == false)
    {
        return QStringLiteral("Command exited with code %1.\n%2")
            .arg(res.exitCode)
            .arg(output.isEmpty() ? res.errorString : output);
    }

    return output.isEmpty() ? QStringLiteral("Command completed successfully (no output).")
                            : output;
}

}  // namespace qcai2
