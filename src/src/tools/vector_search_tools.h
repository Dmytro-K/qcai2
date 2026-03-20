#pragma once

#include "i_tool.h"

namespace qcai2
{

class vector_search_tool_t final : public i_tool_t
{
public:
    QString name() const override
    {
        return QStringLiteral("vector_search");
    }

    QString description() const override
    {
        return QStringLiteral("Run semantic vector search over indexed project files. "
                              "Args: query (required), limit (optional, default 8).");
    }

    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class vector_search_history_tool_t final : public i_tool_t
{
public:
    QString name() const override
    {
        return QStringLiteral("vector_search_history");
    }

    QString description() const override
    {
        return QStringLiteral(
            "Run semantic vector search over indexed chat history for the current "
            "workspace. Args: query (required), limit (optional, default 8).");
    }

    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

}  // namespace qcai2
