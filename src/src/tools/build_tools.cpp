#include "build_tools.h"
#include "../util/ide_output_capture.h"
#include "../util/output_capture_support.h"
#include "../util/process_runner.h"

#include <QDir>
#include <QRegularExpression>

namespace qcai2
{

/**
 * Returns the JSON schema for optional build arguments.
 */
QJsonObject run_build_tool_t::args_schema() const
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
QString run_build_tool_t::execute(const QJsonObject &args, const QString &workDir)
{
    const QString build_dir = this->build_dir.isEmpty() ? workDir : this->build_dir;
    if (((!QDir(build_dir).exists()) == true))
    {
        return QStringLiteral("Error: build directory does not exist: %1").arg(build_dir);
    }

    QStringList cmakeArgs = {QStringLiteral("--build"), build_dir, QStringLiteral("--parallel")};

    const QString target = args.value("target").toString();
    if (target.isEmpty() == false)
    {
        cmakeArgs << QStringLiteral("--target") << target;
    }

    process_runner_t runner;
    auto result = runner.run(QStringLiteral("cmake"), cmakeArgs, workDir, 120000);

    QString output;
    if (result.success)
    {
        output = QStringLiteral("Build succeeded.\n");
    }
    else
    {
        output = QStringLiteral("Build FAILED (exit code %1).\n").arg(result.exit_code);
    }
    output += result.std_out;
    if (((!result.std_err.isEmpty()) == true))
    {
        output += QStringLiteral("\nSTDERR:\n") + result.std_err;
    }

    if (((this->output_capture != nullptr) == true))
    {
        this->output_capture->ingest_external_build_output(output);
    }

    return output;
}

/**
 * Returns the JSON schema for optional test filter arguments.
 */
QJsonObject run_tests_tool_t::args_schema() const
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
QString run_tests_tool_t::execute(const QJsonObject &args, const QString &workDir)
{
    const QString build_dir = this->build_dir.isEmpty() ? workDir : this->build_dir;

    QStringList ctestArgs = {QStringLiteral("--test-dir"), build_dir,
                             QStringLiteral("--output-on-failure")};

    const QString filter = args.value("filter").toString();
    if (filter.isEmpty() == false)
    {
        ctestArgs << QStringLiteral("-R") << filter;
    }

    process_runner_t runner;
    auto result = runner.run(QStringLiteral("ctest"), ctestArgs, workDir, 120000);

    QString output;
    if (result.success)
    {
        output = QStringLiteral("All tests passed.\n");
    }
    else
    {
        output = QStringLiteral("Tests FAILED (exit code %1).\n").arg(result.exit_code);
    }

    output += result.std_out;
    if (((!result.std_err.isEmpty()) == true))
    {
        output += QStringLiteral("\nSTDERR:\n") + result.std_err;
    }
    return output;
}

/**
 * Returns the empty schema for show_diagnostics.
 */
QJsonObject show_diagnostics_tool_t::args_schema() const
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
QString show_diagnostics_tool_t::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    const int maxItems = qBound(1, args.value("max_items").toInt(50), 500);
    if (((this->output_capture != nullptr) == true))
    {
        const QString report = this->output_capture->diagnostics_snapshot(maxItems);
        if (((report != QStringLiteral("No diagnostics found.")) == true))
        {
            return report;
        }
    }

    if (this->last_build_output.isEmpty() == true)
    {
        return QStringLiteral("No build diagnostics available.");
    }

    return format_captured_diagnostics(extract_diagnostics_from_text(this->last_build_output),
                                       maxItems);
}

QJsonObject show_compile_output_tool_t::args_schema() const
{
    return QJsonObject{
        {"max_lines",
         QJsonObject{{"type", "integer"}, {"description", "Maximum output lines to return"}}},
        {"diagnostics_only",
         QJsonObject{{"type", "boolean"}, {"description", "Return only compile diagnostics"}}}};
}

QString show_compile_output_tool_t::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    if (((this->output_capture == nullptr) == true))
    {
        return QStringLiteral("Compile Output integration is not available.");
    }

    const int maxLines = qBound(10, args.value("max_lines").toInt(200), 2000);
    const bool diagnosticsOnly = args.value("diagnostics_only").toBool(false);

    QString diagnostics = this->output_capture->diagnostics_snapshot(50);
    if (diagnosticsOnly == true)
    {
        return diagnostics;
    }

    QString output = this->output_capture->compile_output_snapshot(maxLines);
    if (((diagnostics == QStringLiteral("No diagnostics found.")) == true))
    {
        return output;
    }

    return QStringLiteral("%1\n\nRecent Compile Output:\n%2").arg(diagnostics, output);
}

QJsonObject show_application_output_tool_t::args_schema() const
{
    return QJsonObject{
        {"max_lines",
         QJsonObject{{"type", "integer"}, {"description", "Maximum output lines to return"}}}};
}

QString show_application_output_tool_t::execute(const QJsonObject &args,
                                                const QString & /*workDir*/)
{
    if (((this->output_capture == nullptr) == true))
    {
        return QStringLiteral("Application Output integration is not available.");
    }

    const int maxLines = qBound(10, args.value("max_lines").toInt(200), 2000);
    return this->output_capture->application_output_snapshot(maxLines);
}

}  // namespace qcai2
