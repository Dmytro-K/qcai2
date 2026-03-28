#include "find_symbol_tool.h"

#include <coreplugin/locator/ilocatorfilter.h>

#include <QJsonObject>

namespace qcai2
{

static constexpr int default_result_count = 20;
static constexpr int results_cap_max = 100;

/**
 * Returns the JSON schema for find_symbol arguments.
 */
QJsonObject find_symbol_tool_t::args_schema() const
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
QString find_symbol_tool_t::execute(const QJsonObject &args, const QString & /*workDir*/)
{
    const QString query = args.value("query").toString();
    if (query.isEmpty() == true)
    {
        return QStringLiteral("Error: 'query' argument is required.");
    }

    int max_results = args.value("max_results").toInt(default_result_count);
    if (max_results < 1)
    {
        max_results = 1;
    }
    if (max_results > results_cap_max)
    {
        max_results = results_cap_max;
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
    const int count = qMin(max_results, static_cast<int>(entries.size()));
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

    if (entries.size() > max_results)
    {
        lines.append(QStringLiteral("... (%1 more results)").arg(entries.size() - max_results));
    }

    return lines.join('\n');
}

}  // namespace qcai2
