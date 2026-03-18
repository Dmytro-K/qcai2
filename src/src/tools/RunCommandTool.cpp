#include "RunCommandTool.h"
#include "../util/ProcessRunner.h"

#include <QJsonObject>

namespace qcai2
{

static constexpr int kTimeoutMs = 30000;

/**
 * Returns the JSON schema for run_command arguments.
 */
QJsonObject run_command_tool_t::args_schema() const
{
    return QJsonObject{{"command", QJsonObject{{"type", "string"}, {"required", true}}}};
}

/**
 * Runs a shell command inside the project directory.
 * @param args JSON object containing the command string.
 * @param workDir Project root used as the working directory.
 * @return Combined stdout/stderr or an error string.
 */
QString run_command_tool_t::execute(const QJsonObject &args, const QString &workDir)
{
    const QString command = args.value("command").toString();
    if (command.isEmpty() == true)
    {
        return QStringLiteral("Error: 'command' argument is required.");
    }

    process_runner_t runner;
#ifdef Q_OS_WIN
    auto res = runner.run(QStringLiteral("cmd.exe"), {QStringLiteral("/C"), command}, workDir,
                          kTimeoutMs);
#else
    auto res = runner.run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command}, workDir,
                          kTimeoutMs);
#endif

    QString output;
    if (res.std_out.isEmpty() == false)
    {
        output += res.std_out;
    }
    if (res.std_err.isEmpty() == false)
    {
        if (output.isEmpty() == false)
        {
            output += QStringLiteral("\n");
        }
        output += QStringLiteral("[stderr]\n") + res.std_err;
    }

    if (res.success == false)
    {
        return QStringLiteral("Command exited with code %1.\n%2")
            .arg(res.exit_code)
            .arg(output.isEmpty() ? res.error_string : output);
    }

    return output.isEmpty() ? QStringLiteral("Command completed successfully (no output).")
                            : output;
}

}  // namespace qcai2
