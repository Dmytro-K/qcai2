#pragma once

#include "ITool.h"

namespace Qcai2 {

// read_file: reads a file (optionally line range) within the project root.
class ReadFileTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("read_file"); }
    QString description() const override {
        return QStringLiteral("Read contents of a file. Args: path (required), start_line, end_line (optional).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;
};

// apply_patch: applies a unified diff. Requires approval unless dry-run.
class ApplyPatchTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("apply_patch"); }
    QString description() const override {
        return QStringLiteral("Apply a unified diff patch to files in the project. Args: diff (required string).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;
    bool requiresApproval() const override { return true; }
};

} // namespace Qcai2
