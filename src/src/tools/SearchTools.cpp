#include "SearchTools.h"
#include "../util/ProcessRunner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace qcai2
{

/**
 * Returns the JSON schema for search_repo arguments.
 */
QJsonObject SearchRepoTool::argsSchema() const
{
    return QJsonObject{{"pattern", QJsonObject{{"type", "string"}, {"required", true}}},
                       {"glob", QJsonObject{{"type", "string"}}},
                       {"max_results", QJsonObject{{"type", "integer"}}}};
}

/**
 * Searches repository files for a regex pattern.
 * @param args JSON object containing the pattern and optional filters.
 * @param workDir Project root used as the search scope.
 * @return Matching lines or an error string.
 */
QString SearchRepoTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString pattern = args.value("pattern").toString();
    if (pattern.isEmpty() == true)
    {
        return QStringLiteral("Error: 'pattern' argument is required.");
    }

    const QString glob = args.value("glob").toString();
    int maxResults = args.value("max_results").toInt(30);
    if (((maxResults < 1) == true))
    {
        maxResults = 30;
    }

    // Try ripgrep first
    const QString rgPath = QStandardPaths::findExecutable(QStringLiteral("rg"));
    if (rgPath.isEmpty() == false)
    {
        return searchWithRipgrep(pattern, glob, maxResults, workDir);
    }

    return searchFallback(pattern, glob, maxResults, workDir);
}

/**
 * Uses ripgrep to search the repository when the executable is available.
 * @param pattern Regular expression to search for.
 * @param glob Optional ripgrep glob filter.
 * @param maxResults Maximum matches to return.
 * @param workDir Project root used as the search scope.
 * @return Matching lines or an error string.
 */
QString SearchRepoTool::searchWithRipgrep(const QString &pattern, const QString &glob,
                                          int maxResults, const QString &workDir)
{
    QStringList args;
    args << QStringLiteral("--no-heading") << QStringLiteral("--line-number")
         << QStringLiteral("--max-count") << QString::number(maxResults);

    if (glob.isEmpty() == false)
    {
        args << QStringLiteral("--glob") << glob;
    }

    args << pattern << QStringLiteral(".");

    ProcessRunner runner;
    auto result = runner.run(QStringLiteral("rg"), args, workDir, 15000);

    if (result.exitCode == 1)
    {
        return QStringLiteral("No matches found.");
    }
    if (!result.success && result.exitCode != 1)
    {
        return QStringLiteral("Error running rg: %1").arg(result.stdErr);
    }

    return result.stdOut;
}

/**
 * Uses Qt file iteration and regex matching as a ripgrep fallback.
 * @param pattern Regular expression to search for.
 * @param glob Optional name filter applied during iteration.
 * @param maxResults Maximum matches to return.
 * @param workDir Project root used as the search scope.
 * @return Matching lines or an error string.
 */
QString SearchRepoTool::searchFallback(const QString &pattern, const QString &glob, int maxResults,
                                       const QString &workDir)
{
    QRegularExpression re(pattern);
    if (re.isValid() == false)
    {
        return QStringLiteral("Error: invalid regex: %1").arg(re.errorString());
    }

    QStringList nameFilters;
    if (glob.isEmpty() == false)
    {
        nameFilters << glob;
    }

    QDirIterator it(workDir,
                    nameFilters.isEmpty() ? QStringList{QStringLiteral("*")} : nameFilters,
                    QDir::Files, QDirIterator::Subdirectories);

    QStringList results;
    int count = 0;
    while (((it.hasNext() && count < maxResults) == true))
    {
        it.next();
        QFile file(it.filePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
        {
            continue;
        }

        QTextStream in(&file);
        int lineNum = 0;
        while (((!in.atEnd() && count < maxResults) == true))
        {
            ++lineNum;
            const QString line = in.readLine();
            if (re.match(line).hasMatch())
            {
                const QString rel = QDir(workDir).relativeFilePath(it.filePath());
                results.append(QStringLiteral("%1:%2:%3").arg(rel).arg(lineNum).arg(line));
                ++count;
            }
        }
    }

    return results.isEmpty() ? QStringLiteral("No matches found.") : results.join('\n');
}

}  // namespace qcai2
