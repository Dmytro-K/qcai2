/*! Shared helpers for web_search and web_fetch tools. */
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace qcai2
{

struct web_tools_config_t
{
    bool enabled = false;
    QString search_provider = QStringLiteral("duckduckgo");
    QString search_endpoint = QStringLiteral("https://html.duckduckgo.com/html/");
    QString search_api_key;
    int search_max_results = 5;
    int request_timeout_sec = 20;
    int fetch_max_chars = 20000;
    bool allow_private_fetch_hosts = false;
};

struct web_search_result_t
{
    QString title;
    QString url;
    QString snippet;
};

struct perplexity_search_response_t
{
    QString answer;
    QList<web_search_result_t> results;
    QStringList citations;
};

QString default_web_search_endpoint(const QString &provider);
bool is_web_fetch_url_allowed(const QUrl &url, bool allow_private_hosts = false,
                              QString *error = nullptr);
QString html_title(const QString &html);
QString html_to_readable_text(const QString &html);
perplexity_search_response_t parse_perplexity_search_response(const QByteArray &payload,
                                                              int max_results,
                                                              QString *error = nullptr);
QList<web_search_result_t> parse_brave_search_results(const QByteArray &payload, int max_results,
                                                      QString *error = nullptr);
QList<web_search_result_t> parse_serper_search_results(const QByteArray &payload, int max_results,
                                                       QString *error = nullptr);
QList<web_search_result_t> parse_duckduckgo_search_results(const QString &html, int max_results);
QString format_web_search_results(const QList<web_search_result_t> &results);
QString format_perplexity_search_results(const perplexity_search_response_t &response);
QString format_web_fetch_result(const QString &url, const QString &title, const QString &content,
                                const QString &content_type, bool truncated);
QString execute_web_search(const web_tools_config_t &config, const QJsonObject &args);
QString execute_web_fetch(const web_tools_config_t &config, const QJsonObject &args);

}  // namespace qcai2
