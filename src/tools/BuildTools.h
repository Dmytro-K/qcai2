#pragma once

#include "ITool.h"

namespace qcai2
{

// run_build: invokes cmake --build on the active build directory.
class RunBuildTool : public ITool
{
public:
    QString name() const override
    {
        return QStringLiteral("run_build");
    }
    QString description() const override
    {
        return QStringLiteral(
            "Build the project using cmake --build. No args required (uses project build dir).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;

    void setBuildDir(const QString &dir)
    {
        m_buildDir = dir;
    }

private:
    QString m_buildDir;
};

// run_tests: invokes ctest in the build directory.
class RunTestsTool : public ITool
{
public:
    QString name() const override
    {
        return QStringLiteral("run_tests");
    }
    QString description() const override
    {
        return QStringLiteral("Run project tests using ctest. No args required.");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;

    void setBuildDir(const QString &dir)
    {
        m_buildDir = dir;
    }

private:
    QString m_buildDir;
};

// show_diagnostics: parses last build output for errors/warnings.
class ShowDiagnosticsTool : public ITool
{
public:
    QString name() const override
    {
        return QStringLiteral("show_diagnostics");
    }
    QString description() const override
    {
        return QStringLiteral("Show last build diagnostics (errors and warnings).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;

    void setLastBuildOutput(const QString &output)
    {
        m_lastBuildOutput = output;
    }

private:
    QString m_lastBuildOutput;
};

}  // namespace qcai2
