#pragma once

#include "ITool.h"

namespace Qcai2 {

// open_file_at_location: opens a file in the Qt Creator editor and sets cursor position.
class OpenFileAtLocationTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("open_file_at_location"); }
    QString description() const override {
        return QStringLiteral("Open a file in the editor at a specific line. "
                              "Args: path (required), line (optional int), column (optional int).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;
};

} // namespace Qcai2
