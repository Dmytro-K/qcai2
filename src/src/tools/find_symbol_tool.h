#pragma once

#include "i_tool.h"

namespace qcai2
{

/**
 * Tool that searches for symbols using Qt Creator's locator infrastructure.
 */
class find_symbol_tool_t : public i_tool_t
{
public:
    /**
     * Returns the stable tool name.
     */
    QString name() const override
    {
        return QStringLiteral("find_symbol");
    }

    /**
     * Returns the prompt description for the tool.
     */
    QString description() const override
    {
        return QStringLiteral("Search for a class, function, or symbol by name in the project. "
                              "Args: query (required string, symbol name or pattern), "
                              "max_results (optional int, default 20). "
                              "Returns matching symbol names with file paths and line numbers.");
    }

    /**
     * Returns the JSON schema for find_symbol arguments.
     */
    QJsonObject args_schema() const override;

    /**
     * Searches for symbols via Qt Creator's CppLocatorFilter.
     * @param args Tool arguments.
     * @param workDir Working directory used by the operation.
     */
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

}  // namespace qcai2
