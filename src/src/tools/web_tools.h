/*! Web search and fetch tools. */
#pragma once

#include "i_tool.h"

namespace qcai2
{

class web_search_tool_t : public i_tool_t
{
public:
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class web_fetch_tool_t : public i_tool_t
{
public:
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

}  // namespace qcai2
