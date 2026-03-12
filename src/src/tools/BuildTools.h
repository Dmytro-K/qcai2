#pragma once

#include "ITool.h"

namespace qcai2
{

class IdeOutputCapture;

/**
 * Tool that runs cmake --build in the configured build directory.
 */
class RunBuildTool : public ITool
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("run_build");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral(
            "Build the project using cmake --build. No args required (uses project build dir).");
    }

    /**
     * Returns the JSON schema for optional build arguments.
     */
    QJsonObject argsSchema() const override;

    /**
     * Executes a project build and returns combined output.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

    /**
     * Overrides the build directory used by the tool.
     * @param dir Directory path.
     */
    void setBuildDir(const QString &dir)
    {
        m_buildDir = dir;
    }

    void setOutputCapture(IdeOutputCapture *outputCapture)
    {
        m_outputCapture = outputCapture;
    }

private:
    /**
     * Build directory used for cmake and ctest commands.
     */
    QString m_buildDir;
    IdeOutputCapture *m_outputCapture = nullptr;

};

/**
 * Tool that runs ctest in the configured build directory.
 */
class RunTestsTool : public ITool
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("run_tests");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Run project tests using ctest. No args required.");
    }

    /**
     * Returns the JSON schema for optional test filter arguments.
     */
    QJsonObject argsSchema() const override;

    /**
     * Executes project tests and returns combined output.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

    /**
     * Overrides the build directory used by the tool.
     * @param dir Directory path.
     */
    void setBuildDir(const QString &dir)
    {
        m_buildDir = dir;
    }

private:
    /**
     * Build directory used for ctest commands.
     */
    QString m_buildDir;

};

/**
 * Tool that extracts diagnostics from cached build output.
 */
class ShowDiagnosticsTool : public ITool
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("show_diagnostics");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Show last build diagnostics (errors and warnings).");
    }

    /**
     * Returns the empty schema for this no-argument tool.
     */
    QJsonObject argsSchema() const override;

    /**
     * Extracts diagnostic lines from the last recorded build output.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

    /**
     * Stores the build output that later diagnostics should inspect.
     * @param output Build output text to cache.
     */
    void setLastBuildOutput(const QString &output)
    {
        m_lastBuildOutput = output;
    }

    void setOutputCapture(IdeOutputCapture *outputCapture)
    {
        m_outputCapture = outputCapture;
    }

private:
    /** Cached build output scanned by show_diagnostics. */
    QString m_lastBuildOutput;
    IdeOutputCapture *m_outputCapture = nullptr;

};

class ShowCompileOutputTool : public ITool
{
public:
    QString name() const override
    {
        return QStringLiteral("show_compile_output");
    }

    QString description() const override
    {
        return QStringLiteral(
            "Show recent Qt Creator Compile Output and compile diagnostics.");
    }

    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;

    void setOutputCapture(IdeOutputCapture *outputCapture)
    {
        m_outputCapture = outputCapture;
    }

private:
    IdeOutputCapture *m_outputCapture = nullptr;

};

class ShowApplicationOutputTool : public ITool
{
public:
    QString name() const override
    {
        return QStringLiteral("show_application_output");
    }

    QString description() const override
    {
        return QStringLiteral("Show recent Qt Creator Application Output.");
    }

    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;

    void setOutputCapture(IdeOutputCapture *outputCapture)
    {
        m_outputCapture = outputCapture;
    }

private:
    IdeOutputCapture *m_outputCapture = nullptr;

};

}  // namespace qcai2
