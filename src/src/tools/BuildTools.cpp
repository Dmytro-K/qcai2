#include "BuildTools.h"
#include "../util/IdeOutputCapture.h"
#include "../util/OutputCaptureSupport.h"
#include "../util/ProcessRunner.h"

#include <QDir>
#include <QRegularExpression>

namespace qcai2
{

/**
 * Returns the JSON schema for optional build arguments.
 */
QJsonObject RunBuildTool::argsSchema() const
{
    return QJsonObject{
        {"target", QJsonObject{{"type", "string"}, {"description", "Optional build target"}}}};
}

/**
 * Runs cmake --build in the configured build directory.
 * @param args JSON object containing an optional build target.
 * @param workDir Project root used as the command working directory.
 * @return Combined build output with a success or failure prefix.
 */
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
    if (result.success)
    {
        output = QStringLiteral("Build succeeded.\n");
    }
    else
    {
        output = QStringLiteral("Build FAILED (exit code %1).\n").arg(result.exitCode);
    }
    output += result.stdOut;
    if (!result.stdErr.isEmpty())
        output += QStringLiteral("\nSTDERR:\n") + result.stdErr;

    if (m_outputCapture != nullptr)
        m_outputCapture->ingestExternalBuildOutput(output);

    return output;
}

/**
 * Returns the JSON schema for optional test filter arguments.
 */
QJsonObject RunTestsTool::argsSchema() const
{
    return QJsonObject{
        {"filter", QJsonObject{{"type", "string"}, {"description", "ctest -R filter regex"}}}};
}

/**
 * Runs ctest in the configured build directory.
 * @param args JSON object containing an optional ctest filter.
 * @param workDir Project root used as the command working directory.
 * @return Combined test output with a success or failure prefix.
 */
QString RunTestsTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString buildDir = m_buildDir.isEmpty() ? workDir : m_buildDir;

    QStringList ctestArgs = {QStringLiteral("--test-dir"), buildDir,
                             QStringLiteral("--output-on-failure")};

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

/**
 * Returns the empty schema for show_diagnostics.
 */
QJsonObject ShowDiagnosticsTool::argsSchema() const
{
    return QJsonObject{
        {"max_items", QJsonObject{{"type", "integer"},
                                  {"description", "Maximum number of diagnostics to return"}}}};
}

/**
 * Extracts diagnostic lines from cached build output.
 * @param args Unused.
 * @param workDir Unused.
 * @return Matching error, warning, or note lines.
 */
QString ShowDiagnosticsTool::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    const int maxItems = qBound(1, args.value("max_items").toInt(50), 500);
    if (m_outputCapture != nullptr)
    {
        const QString report = m_outputCapture->diagnosticsSnapshot(maxItems);
        if (report != QStringLiteral("No diagnostics found."))
            return report;
    }

    if (m_lastBuildOutput.isEmpty())
        return QStringLiteral("No build diagnostics available.");

    return formatCapturedDiagnostics(extractDiagnosticsFromText(m_lastBuildOutput), maxItems);
}

QJsonObject ShowCompileOutputTool::argsSchema() const
{
    return QJsonObject{
        {"max_lines", QJsonObject{{"type", "integer"},
                                  {"description", "Maximum output lines to return"}}},
        {"diagnostics_only", QJsonObject{{"type", "boolean"},
                                         {"description", "Return only compile diagnostics"}}}};
}

QString ShowCompileOutputTool::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    if (m_outputCapture == nullptr)
        return QStringLiteral("Compile Output integration is not available.");

    const int maxLines = qBound(10, args.value("max_lines").toInt(200), 2000);
    const bool diagnosticsOnly = args.value("diagnostics_only").toBool(false);

    QString diagnostics = m_outputCapture->diagnosticsSnapshot(50);
    if (diagnosticsOnly)
        return diagnostics;

    QString output = m_outputCapture->compileOutputSnapshot(maxLines);
    if (diagnostics == QStringLiteral("No diagnostics found."))
        return output;

    return QStringLiteral("%1\n\nRecent Compile Output:\n%2").arg(diagnostics, output);
}

QJsonObject ShowApplicationOutputTool::argsSchema() const
{
    return QJsonObject{
        {"max_lines", QJsonObject{{"type", "integer"},
                                  {"description", "Maximum output lines to return"}}}};
}

QString ShowApplicationOutputTool::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    if (m_outputCapture == nullptr)
        return QStringLiteral("Application Output integration is not available.");

    const int maxLines = qBound(10, args.value("max_lines").toInt(200), 2000);
    return m_outputCapture->applicationOutputSnapshot(maxLines);
}

}  // namespace qcai2
