#pragma once

#include "ITool.h"

namespace qcai2
{

/**
 * Tool that searches repository text with ripgrep or a Qt fallback.
 */
class search_repo_tool_t : public i_tool_t
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
    QJsonObject args_schema() const override;

    /**
     * Searches the repository and returns matching lines.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;

private:
    /**
     * Uses ripgrep when available for fast regex search.
     * @param pattern Regular expression pattern to search for.
     * @param glob Optional file glob used to filter matches.
     * @param maxResults Maximum number of matches to return.
     * @param workDir Working directory used by the operation.
     */
    QString search_with_ripgrep(const QString &pattern, const QString &glob, int max_results,
                                const QString &work_dir);

    /**
     * Uses QDirIterator and QRegularExpression when ripgrep is unavailable.
     * @param pattern Regular expression pattern to search for.
     * @param glob Optional file glob used to filter matches.
     * @param maxResults Maximum number of matches to return.
     * @param workDir Working directory used by the operation.
     */
    QString search_fallback(const QString &pattern, const QString &glob, int max_results,
                            const QString &work_dir);
};

}  // namespace qcai2
