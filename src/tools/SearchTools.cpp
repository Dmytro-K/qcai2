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

QJsonObject SearchRepoTool::argsSchema() const
{
    return QJsonObject{{"pattern", QJsonObject{{"type", "string"}, {"required", true}}},
                       {"glob", QJsonObject{{"type", "string"}}},
                       {"max_results", QJsonObject{{"type", "integer"}}}};
}

QString SearchRepoTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString pattern = args.value("pattern").toString();
    if (pattern.isEmpty())
        return QStringLiteral("Error: 'pattern' argument is required.");

    const QString glob = args.value("glob").toString();
    int maxResults = args.value("max_results").toInt(30);
    if (maxResults < 1)
        maxResults = 30;

    // Try ripgrep first
    const QString rgPath = QStandardPaths::findExecutable(QStringLiteral("rg"));
    if (!rgPath.isEmpty())
        return searchWithRipgrep(pattern, glob, maxResults, workDir);

    return searchFallback(pattern, glob, maxResults, workDir);
}

QString SearchRepoTool::searchWithRipgrep(const QString &pattern, const QString &glob,
                                          int maxResults, const QString &workDir)
{
    QStringList args;
    args << QStringLiteral("--no-heading") << QStringLiteral("--line-number")
         << QStringLiteral("--max-count") << QString::number(maxResults);

    if (!glob.isEmpty())
        args << QStringLiteral("--glob") << glob;

    args << pattern << QStringLiteral(".");

    ProcessRunner runner;
    auto result = runner.run(QStringLiteral("rg"), args, workDir, 15000);

    if (result.exitCode == 1)
        return QStringLiteral("No matches found.");
    if (!result.success && result.exitCode != 1)
        return QStringLiteral("Error running rg: %1").arg(result.stdErr);

    return result.stdOut;
}

QString SearchRepoTool::searchFallback(const QString &pattern, const QString &glob, int maxResults,
                                       const QString &workDir)
{
    QRegularExpression re(pattern);
    if (!re.isValid())
        return QStringLiteral("Error: invalid regex: %1").arg(re.errorString());

    QStringList nameFilters;
    if (!glob.isEmpty())
        nameFilters << glob;

    QDirIterator it(workDir,
                    nameFilters.isEmpty() ? QStringList{QStringLiteral("*")} : nameFilters,
                    QDir::Files, QDirIterator::Subdirectories);

    QStringList results;
    int count = 0;
    while (it.hasNext() && count < maxResults)
    {
        it.next();
        QFile file(it.filePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QTextStream in(&file);
        int lineNum = 0;
        while (!in.atEnd() && count < maxResults)
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
