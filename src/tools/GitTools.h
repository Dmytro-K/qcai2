#pragma once

#include "ITool.h"

namespace Qcai2 {

// git_status: runs "git status --porcelain" in the project directory.
class GitStatusTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("git_status"); }
    QString description() const override {
        return QStringLiteral("Show git status (read-only). No args required.");
    }
    QJsonObject argsSchema() const override { return {}; }
    QString execute(const QJsonObject &args, const QString &workDir) override;
};

// git_diff: runs "git diff" in the project directory.
class GitDiffTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("git_diff"); }
    QString description() const override {
        return QStringLiteral("Show git diff (read-only). Args: path (optional, specific file).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;
};

} // namespace Qcai2
