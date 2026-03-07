#pragma once

#include "ITool.h"

namespace qcai2
{

/**
 * Tool that searches repository text with ripgrep or a Qt fallback.
 */
class SearchRepoTool : public ITool
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("search_repo");
    }
    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Search for a regex pattern in the project files. "
                              "Args: pattern (required), glob (optional file glob), max_results "
                              "(optional, default 30).");
    }
    /**
     * Returns the JSON schema for repository search arguments.
     */
    QJsonObject argsSchema() const override;
    /**
     * Searches the repository and returns matching lines.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &workDir) override;

private:
    /**
     * Uses ripgrep when available for fast regex search.
     * @param pattern Regular expression pattern to search for.
     * @param glob Optional file glob used to filter matches.
     * @param maxResults Maximum number of matches to return.
     * @param workDir Working directory used by the operation.
     */
    QString searchWithRipgrep(const QString &pattern, const QString &glob, int maxResults,
                              const QString &workDir);
    /**
     * Uses QDirIterator and QRegularExpression when ripgrep is unavailable.
     * @param pattern Regular expression pattern to search for.
     * @param glob Optional file glob used to filter matches.
     * @param maxResults Maximum number of matches to return.
     * @param workDir Working directory used by the operation.
     */
    QString searchFallback(const QString &pattern, const QString &glob, int maxResults,
                           const QString &workDir);
};

}  // namespace qcai2
