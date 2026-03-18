#pragma once

#include "i_tool.h"

namespace qcai2
{

class ide_output_capture_t;

/**
 * Tool that runs cmake --build in the configured build directory.
 */
class run_build_tool_t : public i_tool_t
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
    QJsonObject args_schema() const override;

    /**
     * Executes a project build and returns combined output.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    /**
     * Overrides the build directory used by the tool.
     * @param dir Directory path.
     */
    void set_build_dir(const QString &dir)
    {
        this->build_dir = dir;
    }

    void set_output_capture(ide_output_capture_t *output_capture)
    {
        this->output_capture = output_capture;
    }

private:
    /**
     * Build directory used for cmake and ctest commands.
     */
    QString build_dir;
    ide_output_capture_t *output_capture = nullptr;
};

/**
 * Tool that runs ctest in the configured build directory.
 */
class run_tests_tool_t : public i_tool_t
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
    QJsonObject args_schema() const override;

    /**
     * Executes project tests and returns combined output.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    /**
     * Overrides the build directory used by the tool.
     * @param dir Directory path.
     */
    void set_build_dir(const QString &dir)
    {
        this->build_dir = dir;
    }

private:
    /**
     * Build directory used for ctest commands.
     */
    QString build_dir;
};

/**
 * Tool that extracts diagnostics from cached build output.
 */
class show_diagnostics_tool_t : public i_tool_t
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
    QJsonObject args_schema() const override;

    /**
     * Extracts diagnostic lines from the last recorded build output.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    /**
     * Stores the build output that later diagnostics should inspect.
     * @param output Build output text to cache.
     */
    void set_last_build_output(const QString &output)
    {
        this->last_build_output = output;
    }

    void set_output_capture(ide_output_capture_t *output_capture)
    {
        this->output_capture = output_capture;
    }

private:
    /** Cached build output scanned by show_diagnostics. */
    QString last_build_output;
    ide_output_capture_t *output_capture = nullptr;
};

class show_compile_output_tool_t : public i_tool_t
{
public:
    QString name() const override
    {
        return QStringLiteral("show_compile_output");
    }

    QString description() const override
    {
        return QStringLiteral("Show recent Qt Creator Compile Output and compile diagnostics.");
    }

    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    void set_output_capture(ide_output_capture_t *output_capture)
    {
        this->output_capture = output_capture;
    }

private:
    ide_output_capture_t *output_capture = nullptr;
};

class show_application_output_tool_t : public i_tool_t
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

    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;

    void set_output_capture(ide_output_capture_t *output_capture)
    {
        this->output_capture = output_capture;
    }

private:
    ide_output_capture_t *output_capture = nullptr;
};

}  // namespace qcai2
