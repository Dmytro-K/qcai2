/*! Web search and fetch tools. */

#include "web_tools.h"

#include "../settings/settings.h"
#include "../util/web_tools.h"

namespace qcai2
{

namespace
{

web_tools_config_t web_tools_config_from_settings()
{
    const settings_t &current_settings = settings();
    return web_tools_config_t{
        current_settings.web_tools_enabled,      current_settings.web_search_provider,
        current_settings.web_search_endpoint,    current_settings.web_search_api_key,
        current_settings.web_search_max_results, current_settings.web_request_timeout_sec,
        current_settings.web_fetch_max_chars,    false,
    };
}

}  // namespace

QString web_search_tool_t::name() const
{
    return QStringLiteral("web_search");
}

QString web_search_tool_t::description() const
{
    return QStringLiteral("Search the public web. Args: query (required), max_results (optional). "
                          "Requires Web Tools to be enabled in settings.");
}

QJsonObject web_search_tool_t::args_schema() const
{
    return QJsonObject{
        {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                              {QStringLiteral("required"), true}}},
        {QStringLiteral("max_results"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
    };
}

QString web_search_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    Q_UNUSED(work_dir);
    return execute_web_search(web_tools_config_from_settings(), args);
}

QString web_fetch_tool_t::name() const
{
    return QStringLiteral("web_fetch");
}

QString web_fetch_tool_t::description() const
{
    return QStringLiteral("Fetch a public web page or text resource and return readable content. "
                          "Args: url (required), max_chars (optional), raw (optional boolean). "
                          "Requires Web Tools to be enabled in settings.");
}

QJsonObject web_fetch_tool_t::args_schema() const
{
    return QJsonObject{
        {QStringLiteral("url"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                            {QStringLiteral("required"), true}}},
        {QStringLiteral("max_chars"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
        {QStringLiteral("raw"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
    };
}

QString web_fetch_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    Q_UNUSED(work_dir);
    return execute_web_fetch(web_tools_config_from_settings(), args);
}

}  // namespace qcai2
