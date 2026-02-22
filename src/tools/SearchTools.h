#pragma once

#include "ITool.h"

namespace Qcai2 {

// search_repo: search for text in files using ripgrep (or fallback to QDirIterator).
class SearchRepoTool : public ITool
{
public:
    QString name() const override { return QStringLiteral("search_repo"); }
    QString description() const override {
        return QStringLiteral("Search for a regex pattern in the project files. "
                              "Args: pattern (required), glob (optional file glob), max_results (optional, default 30).");
    }
    QJsonObject argsSchema() const override;
    QString execute(const QJsonObject &args, const QString &workDir) override;

private:
    QString searchWithRipgrep(const QString &pattern, const QString &glob,
                              int maxResults, const QString &workDir);
    QString searchFallback(const QString &pattern, const QString &glob,
                           int maxResults, const QString &workDir);
};

} // namespace Qcai2
