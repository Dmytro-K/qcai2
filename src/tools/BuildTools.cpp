#include "BuildTools.h"
#include "../util/ProcessRunner.h"

#include <QDir>
#include <QRegularExpression>

namespace Qcai2 {

// ---------------------------------------------------------------------------
// RunBuildTool
// ---------------------------------------------------------------------------

QJsonObject RunBuildTool::argsSchema() const
{
    return QJsonObject{
        {"target", QJsonObject{{"type", "string"}, {"description", "Optional build target"}}}
    };
}

QString RunBuildTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString buildDir = m_buildDir.isEmpty() ? workDir : m_buildDir;
    if (!QDir(buildDir).exists())
        return QStringLiteral("Error: build directory does not exist: %1").arg(buildDir);

    QStringList cmakeArgs = {QStringLiteral("--build"), buildDir, QStringLiteral("--parallel")};

    const QString target = args.value("target").toString();
    if (!target.isEmpty())
        cmakeArgs << QStringLiteral("--target") << target;

    ProcessRunner runner;
    auto result = runner.run(QStringLiteral("cmake"), cmakeArgs, workDir, 120000);

    QString output;
    if (result.success) {
        output = QStringLiteral("Build succeeded.\n");
    } else {
        output = QStringLiteral("Build FAILED (exit code %1).\n").arg(result.exitCode);
    }
    output += result.stdOut;
    if (!result.stdErr.isEmpty())
        output += QStringLiteral("\nSTDERR:\n") + result.stdErr;

    return output;
}

// ---------------------------------------------------------------------------
// RunTestsTool
// ---------------------------------------------------------------------------

QJsonObject RunTestsTool::argsSchema() const
{
    return QJsonObject{
        {"filter", QJsonObject{{"type", "string"}, {"description", "ctest -R filter regex"}}}
    };
}

QString RunTestsTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString buildDir = m_buildDir.isEmpty() ? workDir : m_buildDir;

    QStringList ctestArgs = {QStringLiteral("--test-dir"), buildDir, QStringLiteral("--output-on-failure")};

    const QString filter = args.value("filter").toString();
    if (!filter.isEmpty())
        ctestArgs << QStringLiteral("-R") << filter;

    ProcessRunner runner;
    auto result = runner.run(QStringLiteral("ctest"), ctestArgs, workDir, 120000);

    QString output;
    if (result.success)
        output = QStringLiteral("All tests passed.\n");
    else
        output = QStringLiteral("Tests FAILED (exit code %1).\n").arg(result.exitCode);

    output += result.stdOut;
    if (!result.stdErr.isEmpty())
        output += QStringLiteral("\nSTDERR:\n") + result.stdErr;
    return output;
}

// ---------------------------------------------------------------------------
// ShowDiagnosticsTool
// ---------------------------------------------------------------------------

QJsonObject ShowDiagnosticsTool::argsSchema() const
{
    return QJsonObject{};
}

QString ShowDiagnosticsTool::execute(const QJsonObject & /*args*/, const QString & /*workDir*/)
{
    if (m_lastBuildOutput.isEmpty())
        return QStringLiteral("No build output available.");

    // Extract lines containing error/warning patterns
    static const QRegularExpression diagRe(
        R"((?:error|warning|note):\s.*)", QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = m_lastBuildOutput.split('\n');
    QStringList diags;
    for (const QString &line : lines) {
        if (diagRe.match(line).hasMatch())
            diags.append(line);
    }

    return diags.isEmpty() ? QStringLiteral("No diagnostics found.") : diags.join('\n');
}

} // namespace Qcai2
