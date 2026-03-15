#include "FindSymbolTool.h"

#include <coreplugin/locator/ilocatorfilter.h>

#include <QJsonObject>

namespace qcai2
{

static constexpr int kDefaultMaxResults = 20;
static constexpr int kMaxResultsCap = 100;

/**
 * Returns the JSON schema for find_symbol arguments.
 */
QJsonObject FindSymbolTool::argsSchema() const
{
    return QJsonObject{{"query", QJsonObject{{"type", "string"}, {"required", true}}},
                       {"max_results", QJsonObject{{"type", "integer"}}}};
}

/**
 * Searches for symbols via Qt Creator's locator infrastructure.
 * @param args JSON object containing the query and optional max_results.
 * @param workDir Project root (unused, locator searches across indexed projects).
 * @return Formatted symbol list or an error string.
 */
QString FindSymbolTool::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    const QString query = args.value("query").toString();
    if (query.isEmpty() == true)
    {
        return QStringLiteral("Error: 'query' argument is required.");
    }

    int maxResults = args.value("max_results").toInt(kDefaultMaxResults);
    if (maxResults < 1)
    {
        maxResults = 1;
    }
    if (maxResults > kMaxResultsCap)
    {
        maxResults = kMaxResultsCap;
    }

    // Use AllSymbols matchers to search across classes, functions, enums etc.
    const auto tasks = Core::LocatorMatcher::matchers(Core::MatcherType::AllSymbols);
    if (tasks.isEmpty() == true)
    {
        return QStringLiteral("Error: no symbol index available (C++ model may not be loaded).");
    }

    // runBlocking starts an internal event loop and returns results synchronously
    const auto entries = Core::LocatorMatcher::runBlocking(tasks, query);

    if (entries.isEmpty() == true)
    {
        return QStringLiteral("No symbols found matching '%1'.").arg(query);
    }

    QStringList lines;
    const int count = qMin(maxResults, static_cast<int>(entries.size()));
    for (int i = 0; i < count; ++i)
    {
        const auto &entry = entries[i];
        QString line = entry.displayName;
        if (entry.extraInfo.isEmpty() == false)
        {
            line += QStringLiteral("  (%1)").arg(entry.extraInfo);
        }
        if (entry.linkForEditor.has_value())
        {
            const auto &link = entry.linkForEditor.value();
            line += QStringLiteral("  [%1:%2]")
                        .arg(link.targetFilePath.toUserOutput())
                        .arg(link.target.line);
        }
        else if (entry.filePath.isEmpty() == false)
        {
            line += QStringLiteral("  [%1]").arg(entry.filePath.toUserOutput());
        }
        lines.append(line);
    }

    if (entries.size() > maxResults)
    {
        lines.append(QStringLiteral("... (%1 more results)").arg(entries.size() - maxResults));
    }

    return lines.join('\n');
}

}  // namespace qcai2
