/*! Shared helpers for web_search and web_fetch tools. */

#include "web_tools.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocumentFragment>
#include <QUrlQuery>

namespace qcai2
{

namespace
{

struct http_response_t
{
    int status_code = 0;
    QByteArray body;
    QString content_type;
    QString final_url;
    QString error;
};

QRegularExpression html_regex(const QString &pattern, bool dot_matches_everything = true)
{
    QRegularExpression::PatternOptions options = QRegularExpression::CaseInsensitiveOption;
    if (dot_matches_everything == true)
    {
        options |= QRegularExpression::DotMatchesEverythingOption;
    }
    return QRegularExpression(pattern, options);
}

QString normalize_whitespace(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QRegularExpression(QStringLiteral(R"([ \t\x{00A0}]+)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral(R"(\n[ \t]+)")), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral(R"(\n{3,})")), QStringLiteral("\n\n"));
    return text.trimmed();
}

QString normalize_inline_text(QString text)
{
    text = normalize_whitespace(std::move(text));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return text.trimmed();
}

QString html_fragment_to_text(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText();
}

QString decoded_url_text(const QString &text)
{
    return normalize_inline_text(html_fragment_to_text(text));
}

QString normalize_duckduckgo_result_url(QString raw_url)
{
    raw_url = decoded_url_text(raw_url);
    if (raw_url.isEmpty() == true)
    {
        return {};
    }

    QUrl url(raw_url);
    if (url.isRelative() == true)
    {
        url = QUrl(QStringLiteral("https://duckduckgo.com")).resolved(url);
    }

    const QUrlQuery query(url);
    if ((url.path() == QStringLiteral("/l") || url.path() == QStringLiteral("/l/")) &&
        query.hasQueryItem(QStringLiteral("uddg")) == true)
    {
        const QString target = query.queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        if (target.isEmpty() == false)
        {
            return QUrl(target).toString();
        }
    }

    return url.toString();
}

bool has_supported_web_scheme(const QUrl &url)
{
    const QString scheme = url.scheme().trimmed().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

bool is_private_ipv4(quint32 address)
{
    return (address & 0xFF000000U) == 0x0A000000U || (address & 0xFF000000U) == 0x7F000000U ||
           (address & 0xFFFF0000U) == 0xA9FE0000U || (address & 0xFFF00000U) == 0xAC100000U ||
           (address & 0xFFFF0000U) == 0xC0A80000U || address == 0;
}

bool is_private_host_name(const QString &host)
{
    const QString normalized_host = host.trimmed().toLower();
    return normalized_host == QStringLiteral("localhost") ||
           normalized_host.endsWith(QStringLiteral(".localhost")) == true ||
           normalized_host.endsWith(QStringLiteral(".local")) == true ||
           normalized_host.endsWith(QStringLiteral(".internal")) == true ||
           normalized_host.endsWith(QStringLiteral(".home.arpa")) == true;
}

bool is_private_host_address(const QString &host)
{
    QHostAddress address;
    if (address.setAddress(host) == false)
    {
        return false;
    }

    if (address.isLoopback() == true)
    {
        return true;
    }

    if (address.protocol() == QAbstractSocket::IPv4Protocol)
    {
        return is_private_ipv4(address.toIPv4Address());
    }

    const QString normalized = host.trimmed().toLower();
    return normalized == QStringLiteral("::1") || normalized.startsWith(QStringLiteral("fc")) ||
           normalized.startsWith(QStringLiteral("fd")) ||
           normalized.startsWith(QStringLiteral("fe80:"));
}

int clamp_max_results(int requested, int fallback)
{
    const int normalized = requested > 0 ? requested : fallback;
    return qBound(1, normalized, 10);
}

int clamp_fetch_chars(int requested, int fallback)
{
    const int normalized = requested > 0 ? requested : fallback;
    return qBound(512, normalized, 100000);
}

int request_timeout_ms(const web_tools_config_t &config)
{
    return qBound(1, config.request_timeout_sec, 300) * 1000;
}

QString normalize_content_type(const QString &content_type)
{
    return content_type.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

bool is_html_content_type(const QString &content_type)
{
    const QString normalized = normalize_content_type(content_type);
    return normalized.contains(QStringLiteral("text/html")) == true ||
           normalized.contains(QStringLiteral("application/xhtml+xml")) == true;
}

bool is_textual_content_type(const QString &content_type)
{
    const QString normalized = normalize_content_type(content_type);
    return normalized.startsWith(QStringLiteral("text/")) == true ||
           normalized == QStringLiteral("application/json") ||
           normalized == QStringLiteral("application/xml") ||
           normalized == QStringLiteral("application/javascript") ||
           normalized == QStringLiteral("application/x-javascript");
}

http_response_t perform_network_request(const QString &method, const QUrl &url,
                                        const QByteArray &body,
                                        const QList<QPair<QByteArray, QByteArray>> &headers,
                                        int timeout_ms)
{
    http_response_t response;
    QNetworkAccessManager network_access_manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout_ms);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
                         QByteArrayLiteral("qcai2-web-tools/1.0"));

    for (const auto &[name, value] : headers)
    {
        request.setRawHeader(name, value);
    }

    QNetworkReply *reply = nullptr;
    if (method == QStringLiteral("POST"))
    {
        reply = network_access_manager.post(request, body);
    }
    else
    {
        reply = network_access_manager.get(request);
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    response.status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.content_type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    response.final_url = reply->url().toString();
    response.body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError)
    {
        response.error = QStringLiteral("HTTP request failed: %1").arg(reply->errorString());
        if (response.body.isEmpty() == false)
        {
            response.error +=
                QStringLiteral(" — %1").arg(QString::fromUtf8(response.body).trimmed().left(500));
        }
    }

    reply->deleteLater();
    return response;
}

QString configured_search_endpoint(const web_tools_config_t &config)
{
    const QString endpoint = config.search_endpoint.trimmed();
    return endpoint.isEmpty() == true ? default_web_search_endpoint(config.search_provider)
                                      : endpoint;
}

QList<web_search_result_t> parse_duckduckgo_lite_links(const QString &html, int max_results)
{
    QList<web_search_result_t> results;
    QSet<QString> seen_urls;
    const QRegularExpression link_re(
        QStringLiteral(R"__(<a\b[^>]*href\s*=\s*"([^"]+)"[^>]*>(.*?)</a>)__"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    auto it = link_re.globalMatch(html);
    while (it.hasNext() == true && results.size() < max_results)
    {
        const QRegularExpressionMatch match = it.next();
        const QString url = normalize_duckduckgo_result_url(match.captured(1));
        const QString title = normalize_inline_text(html_fragment_to_text(match.captured(2)));
        if (url.isEmpty() == true || title.isEmpty() == true)
        {
            continue;
        }
        if (url.startsWith(QStringLiteral("https://duckduckgo.com/")) == true &&
            url.contains(QStringLiteral("?q=")) == true)
        {
            continue;
        }
        if (seen_urls.contains(url) == true)
        {
            continue;
        }
        seen_urls.insert(url);
        results.append({title, url, {}});
    }
    return results;
}

QString body_to_text(const QByteArray &body, const QString &content_type, bool raw)
{
    if (raw == true)
    {
        return QString::fromUtf8(body);
    }

    if (is_html_content_type(content_type) == true)
    {
        return html_to_readable_text(QString::fromUtf8(body));
    }

    if (normalize_content_type(content_type) == QStringLiteral("application/json"))
    {
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error == QJsonParseError::NoError)
        {
            return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
        }
    }

    return QString::fromUtf8(body);
}

QString first_non_empty_json_string(const QJsonValue &value)
{
    if (value.isString() == true)
    {
        return value.toString().trimmed();
    }

    if (value.isArray() == true)
    {
        for (const QJsonValue &item : value.toArray())
        {
            const QString text = item.toString().trimmed();
            if (text.isEmpty() == false)
            {
                return text;
            }
        }
    }

    return {};
}

QStringList json_string_list(const QJsonValue &value)
{
    QStringList values;
    if (value.isArray() == false)
    {
        return values;
    }

    for (const QJsonValue &item : value.toArray())
    {
        const QString text = item.toString().trimmed();
        if (text.isEmpty() == false && values.contains(text) == false)
        {
            values.append(text);
        }
    }

    return values;
}

}  // namespace

QString default_web_search_endpoint(const QString &provider)
{
    const QString normalized_provider = provider.trimmed().toLower();
    if (normalized_provider == QStringLiteral("serper"))
    {
        return QStringLiteral("https://google.serper.dev/search");
    }
    if (normalized_provider == QStringLiteral("brave"))
    {
        return QStringLiteral("https://api.search.brave.com/res/v1/web/search");
    }
    if (normalized_provider == QStringLiteral("perplexity"))
    {
        return QStringLiteral("https://api.perplexity.ai/chat/completions");
    }
    return QStringLiteral("https://html.duckduckgo.com/html/");
}

bool is_web_fetch_url_allowed(const QUrl &url, bool allow_private_hosts, QString *error)
{
    if (url.isValid() == false || url.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("URL is invalid.");
        }
        return false;
    }

    if (has_supported_web_scheme(url) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Only http:// and https:// URLs are supported.");
        }
        return false;
    }

    const QString host = url.host().trimmed();
    if (host.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("URL host is empty.");
        }
        return false;
    }

    if (allow_private_hosts == true)
    {
        return true;
    }

    if (is_private_host_name(host) == true || is_private_host_address(host) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Private, local, or loopback hosts are not allowed.");
        }
        return false;
    }

    return true;
}

QString html_title(const QString &html)
{
    const QRegularExpression title_re =
        html_regex(QStringLiteral(R"(<title\b[^>]*>(.*?)</title>)"));
    const QRegularExpressionMatch match = title_re.match(html);
    if (match.hasMatch() == false)
    {
        return {};
    }
    return normalize_inline_text(html_fragment_to_text(match.captured(1)));
}

QString html_to_readable_text(const QString &html)
{
    QString sanitized = html;
    sanitized.remove(html_regex(QStringLiteral(R"(<script\b[^>]*>.*?</script>)")));
    sanitized.remove(html_regex(QStringLiteral(R"(<style\b[^>]*>.*?</style>)")));
    sanitized.remove(html_regex(QStringLiteral(R"(<noscript\b[^>]*>.*?</noscript>)")));
    sanitized.remove(html_regex(QStringLiteral(R"(<svg\b[^>]*>.*?</svg>)")));
    sanitized.replace(html_regex(QStringLiteral(R"(<br\s*/?>)"), false), QStringLiteral("\n"));
    sanitized.replace(
        html_regex(
            QStringLiteral(
                R"(</(p|div|section|article|main|aside|header|footer|tr|table|ul|ol|li|h[1-6])>)"),
            false),
        QStringLiteral("\n"));
    sanitized.replace(html_regex(QStringLiteral(R"(<li\b[^>]*>)"), false), QStringLiteral("\n- "));
    return normalize_whitespace(html_fragment_to_text(sanitized));
}

QList<web_search_result_t> parse_serper_search_results(const QByteArray &payload, int max_results,
                                                       QString *error)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Invalid Serper response JSON: %1").arg(parse_error.errorString());
        }
        return {};
    }

    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Serper response root must be a JSON object.");
        }
        return {};
    }

    QList<web_search_result_t> results;
    const QJsonObject root = document.object();
    const QJsonArray organic = root.value(QStringLiteral("organic")).toArray();
    for (const QJsonValue &value : organic)
    {
        if (results.size() >= max_results)
        {
            break;
        }

        const QJsonObject object = value.toObject();
        const QString title = object.value(QStringLiteral("title")).toString().trimmed();
        const QString url = object.value(QStringLiteral("link")).toString().trimmed();
        const QString snippet = object.value(QStringLiteral("snippet")).toString().trimmed();
        if (title.isEmpty() == true || url.isEmpty() == true)
        {
            continue;
        }
        results.append({title, url, snippet});
    }

    if (results.isEmpty() == true)
    {
        const QJsonObject answer_box = root.value(QStringLiteral("answerBox")).toObject();
        const QString title = answer_box.value(QStringLiteral("title")).toString().trimmed();
        const QString url = answer_box.value(QStringLiteral("link")).toString().trimmed();
        const QString snippet = answer_box.value(QStringLiteral("snippet")).toString().trimmed();
        if (title.isEmpty() == false && url.isEmpty() == false)
        {
            results.append({title, url, snippet});
        }
    }

    return results;
}

perplexity_search_response_t parse_perplexity_search_response(const QByteArray &payload,
                                                              int max_results, QString *error)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid Perplexity response JSON: %1")
                         .arg(parse_error.errorString());
        }
        return {};
    }

    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Perplexity response root must be a JSON object.");
        }
        return {};
    }

    const QJsonObject root = document.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    const QJsonObject first_choice =
        choices.isEmpty() == false ? choices.at(0).toObject() : QJsonObject();
    const QJsonObject message = first_choice.value(QStringLiteral("message")).toObject();

    perplexity_search_response_t response;
    response.answer = message.value(QStringLiteral("content")).toString().trimmed();
    response.citations = json_string_list(message.value(QStringLiteral("citations")));
    if (response.citations.isEmpty() == true)
    {
        response.citations = json_string_list(root.value(QStringLiteral("citations")));
    }

    QJsonArray raw_results = message.value(QStringLiteral("search_results")).toArray();
    if (raw_results.isEmpty() == true)
    {
        raw_results = root.value(QStringLiteral("search_results")).toArray();
    }

    for (const QJsonValue &value : raw_results)
    {
        if (response.results.size() >= max_results)
        {
            break;
        }

        const QJsonObject object = value.toObject();
        const QString url = object.value(QStringLiteral("url")).toString().trimmed();
        QString title = object.value(QStringLiteral("title")).toString().trimmed();
        QString snippet = first_non_empty_json_string(object.value(QStringLiteral("snippet")));
        if (snippet.isEmpty() == true)
        {
            snippet = first_non_empty_json_string(object.value(QStringLiteral("description")));
        }
        if (snippet.isEmpty() == true)
        {
            snippet = first_non_empty_json_string(object.value(QStringLiteral("content")));
        }
        if (url.isEmpty() == true)
        {
            continue;
        }
        if (title.isEmpty() == true)
        {
            title = url;
        }

        response.results.append({title, url, snippet});
    }

    return response;
}

QList<web_search_result_t> parse_brave_search_results(const QByteArray &payload, int max_results,
                                                      QString *error)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid Brave Search response JSON: %1")
                         .arg(parse_error.errorString());
        }
        return {};
    }

    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Brave Search response root must be a JSON object.");
        }
        return {};
    }

    const QJsonObject root = document.object();
    const QJsonObject web = root.value(QStringLiteral("web")).toObject();
    const QJsonArray raw_results = web.value(QStringLiteral("results")).toArray();

    QList<web_search_result_t> results;
    for (const QJsonValue &value : raw_results)
    {
        if (results.size() >= max_results)
        {
            break;
        }

        const QJsonObject object = value.toObject();
        const QString title = object.value(QStringLiteral("title")).toString().trimmed();
        const QString url = object.value(QStringLiteral("url")).toString().trimmed();
        const QString snippet =
            first_non_empty_json_string(object.value(QStringLiteral("description")));
        const QString extra_snippet =
            first_non_empty_json_string(object.value(QStringLiteral("extra_snippets")));
        if (title.isEmpty() == true || url.isEmpty() == true)
        {
            continue;
        }

        results.append({title, url, snippet.isEmpty() == false ? snippet : extra_snippet});
    }

    return results;
}

QList<web_search_result_t> parse_duckduckgo_search_results(const QString &html, int max_results)
{
    QList<web_search_result_t> results;
    QSet<QString> seen_urls;

    const QRegularExpression anchor_re(
        QStringLiteral(
            R"__(<a\b[^>]*class\s*=\s*"[^"]*\bresult__a\b[^"]*"[^>]*href\s*=\s*"([^"]+)"[^>]*>(.*?)</a>)__"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression snippet_re(
        QStringLiteral(
            R"(<(?:a|div|span|td)\b[^>]*class\s*=\s*"[^"]*(?:result__snippet|result-snippet)[^"]*"[^>]*>(.*?)</(?:a|div|span|td)>)"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);

    auto it = anchor_re.globalMatch(html);
    while (it.hasNext() == true && results.size() < max_results)
    {
        const QRegularExpressionMatch match = it.next();
        const QString url = normalize_duckduckgo_result_url(match.captured(1));
        const QString title = normalize_inline_text(html_fragment_to_text(match.captured(2)));
        if (url.isEmpty() == true || title.isEmpty() == true || seen_urls.contains(url) == true)
        {
            continue;
        }

        const QString tail = html.mid(match.capturedEnd(0), 4000);
        const QRegularExpressionMatch snippet_match = snippet_re.match(tail);
        const QString snippet =
            snippet_match.hasMatch() == true
                ? normalize_inline_text(html_fragment_to_text(snippet_match.captured(1)))
                : QString();
        seen_urls.insert(url);
        results.append({title, url, snippet});
    }

    if (results.isEmpty() == true)
    {
        return parse_duckduckgo_lite_links(html, max_results);
    }

    return results;
}

QString format_web_search_results(const QList<web_search_result_t> &results)
{
    if (results.isEmpty() == true)
    {
        return QStringLiteral("No web results found.");
    }

    QStringList lines;
    for (qsizetype index = 0; index < results.size(); ++index)
    {
        const web_search_result_t &result = results.at(index);
        lines.append(QStringLiteral("%1. [%2](%3)").arg(index + 1).arg(result.title, result.url));
        if (result.snippet.isEmpty() == false)
        {
            lines.append(QStringLiteral("   %1").arg(result.snippet));
        }
        lines.append(QString());
    }

    return lines.join(QLatin1Char('\n')).trimmed();
}

QString format_perplexity_search_results(const perplexity_search_response_t &response)
{
    QStringList lines;
    if (response.answer.isEmpty() == false)
    {
        lines.append(QStringLiteral("Perplexity answer:"));
        lines.append(response.answer.trimmed());
    }

    if (response.results.isEmpty() == false)
    {
        if (lines.isEmpty() == false)
        {
            lines.append(QString());
            lines.append(QStringLiteral("Search results:"));
        }
        lines.append(format_web_search_results(response.results));
    }

    QSet<QString> result_urls;
    for (const web_search_result_t &result : response.results)
    {
        if (result.url.isEmpty() == false)
        {
            result_urls.insert(result.url);
        }
    }

    QStringList extra_citations;
    for (const QString &citation : response.citations)
    {
        if (citation.isEmpty() == false && result_urls.contains(citation) == false &&
            extra_citations.contains(citation) == false)
        {
            extra_citations.append(citation);
        }
    }

    if (extra_citations.isEmpty() == false)
    {
        if (lines.isEmpty() == false)
        {
            lines.append(QString());
        }
        lines.append(QStringLiteral("Citations:"));
        for (qsizetype index = 0; index < extra_citations.size(); ++index)
        {
            lines.append(QStringLiteral("%1. %2").arg(index + 1).arg(extra_citations.at(index)));
        }
    }

    if (lines.isEmpty() == true)
    {
        return QStringLiteral("No web results found.");
    }

    return lines.join(QLatin1Char('\n')).trimmed();
}

QString format_web_fetch_result(const QString &url, const QString &title, const QString &content,
                                const QString &content_type, bool truncated)
{
    QStringList lines;
    if (title.isEmpty() == false)
    {
        lines.append(QStringLiteral("# %1").arg(title));
        lines.append(QString());
    }
    lines.append(QStringLiteral("URL: %1").arg(url));
    if (content_type.isEmpty() == false)
    {
        lines.append(QStringLiteral("Content-Type: %1").arg(normalize_content_type(content_type)));
    }
    lines.append(QString());
    lines.append(content.trimmed());
    if (truncated == true)
    {
        lines.append(QString());
        lines.append(QStringLiteral("[truncated to configured fetch limit]"));
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString execute_web_search(const web_tools_config_t &config, const QJsonObject &args)
{
    if (config.enabled == false)
    {
        return QStringLiteral("Error: Web tools are disabled. Enable them in Settings > Qcai2 > "
                              "Web Tools.");
    }

    const QString query = args.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty() == true)
    {
        return QStringLiteral("Error: 'query' argument is required.");
    }

    const int max_results = clamp_max_results(
        args.value(QStringLiteral("max_results")).toInt(config.search_max_results),
        config.search_max_results);
    const QString provider = config.search_provider.trimmed().toLower();
    const QString endpoint = configured_search_endpoint(config);
    const QUrl endpoint_url(endpoint);
    if (endpoint_url.isValid() == false || has_supported_web_scheme(endpoint_url) == false)
    {
        return QStringLiteral(
            "Error: Web search endpoint must be a valid http:// or https:// URL.");
    }

    http_response_t response;
    QList<web_search_result_t> results;
    QString parse_error;
    if (provider == QStringLiteral("serper"))
    {
        if (config.search_api_key.trimmed().isEmpty() == true)
        {
            return QStringLiteral(
                "Error: Serper provider requires a configured API key in Web Tools settings.");
        }

        QList<QPair<QByteArray, QByteArray>> headers{
            {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")},
            {QByteArrayLiteral("X-API-KEY"), config.search_api_key.toUtf8()},
        };
        const QJsonObject request_body{{QStringLiteral("q"), query},
                                       {QStringLiteral("num"), max_results}};
        response =
            perform_network_request(QStringLiteral("POST"), endpoint_url,
                                    QJsonDocument(request_body).toJson(QJsonDocument::Compact),
                                    headers, request_timeout_ms(config));
        if (response.error.isEmpty() == false)
        {
            return QStringLiteral("Error: %1").arg(response.error);
        }
        results = parse_serper_search_results(response.body, max_results, &parse_error);
    }
    else if (provider == QStringLiteral("perplexity"))
    {
        if (config.search_api_key.trimmed().isEmpty() == true)
        {
            return QStringLiteral(
                "Error: Perplexity provider requires a configured API key in Web Tools settings.");
        }

        const QJsonArray messages{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"),
                         QStringLiteral("You are a concise web search assistant. Return a brief "
                                        "grounded answer with citations and include structured "
                                        "search results when available.")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), query}},
        };
        const QJsonObject request_body{
            {QStringLiteral("model"), QStringLiteral("sonar")},
            {QStringLiteral("messages"), messages},
            {QStringLiteral("temperature"), 0},
            {QStringLiteral("max_tokens"), 512},
        };
        response = perform_network_request(
            QStringLiteral("POST"), endpoint_url,
            QJsonDocument(request_body).toJson(QJsonDocument::Compact),
            {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")},
             {QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")},
             {QByteArrayLiteral("Authorization"),
              QStringLiteral("Bearer %1").arg(config.search_api_key).toUtf8()}},
            request_timeout_ms(config));
        if (response.error.isEmpty() == false)
        {
            return QStringLiteral("Error: %1").arg(response.error);
        }

        const perplexity_search_response_t perplexity_response =
            parse_perplexity_search_response(response.body, max_results, &parse_error);
        if (parse_error.isEmpty() == false)
        {
            return QStringLiteral("Error: %1").arg(parse_error);
        }
        return format_perplexity_search_results(perplexity_response);
    }
    else if (provider == QStringLiteral("brave"))
    {
        if (config.search_api_key.trimmed().isEmpty() == true)
        {
            return QStringLiteral("Error: Brave Search provider requires a configured API key in "
                                  "Web Tools settings.");
        }

        QUrlQuery url_query(endpoint_url);
        url_query.addQueryItem(QStringLiteral("q"), query);
        url_query.addQueryItem(QStringLiteral("count"), QString::number(max_results));
        QUrl request_url(endpoint_url);
        request_url.setQuery(url_query);
        response = perform_network_request(
            QStringLiteral("GET"), request_url, {},
            {{QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")},
             {QByteArrayLiteral("X-Subscription-Token"), config.search_api_key.toUtf8()}},
            request_timeout_ms(config));
        if (response.error.isEmpty() == false)
        {
            return QStringLiteral("Error: %1").arg(response.error);
        }
        results = parse_brave_search_results(response.body, max_results, &parse_error);
    }
    else
    {
        QUrlQuery url_query(endpoint_url);
        url_query.addQueryItem(QStringLiteral("q"), query);
        QUrl request_url(endpoint_url);
        request_url.setQuery(url_query);
        response = perform_network_request(
            QStringLiteral("GET"), request_url, {},
            {{QByteArrayLiteral("Accept"),
              QByteArrayLiteral("text/html,application/xhtml+xml;q=0.9,text/plain;q=0.8")}},
            request_timeout_ms(config));
        if (response.error.isEmpty() == false)
        {
            return QStringLiteral("Error: %1").arg(response.error);
        }
        results = parse_duckduckgo_search_results(QString::fromUtf8(response.body), max_results);
    }

    if (parse_error.isEmpty() == false)
    {
        return QStringLiteral("Error: %1").arg(parse_error);
    }

    return format_web_search_results(results);
}

QString execute_web_fetch(const web_tools_config_t &config, const QJsonObject &args)
{
    if (config.enabled == false)
    {
        return QStringLiteral("Error: Web tools are disabled. Enable them in Settings > Qcai2 > "
                              "Web Tools.");
    }

    const QString url_text = args.value(QStringLiteral("url")).toString().trimmed();
    if (url_text.isEmpty() == true)
    {
        return QStringLiteral("Error: 'url' argument is required.");
    }

    const bool raw = args.value(QStringLiteral("raw")).toBool(false);
    const int max_chars =
        clamp_fetch_chars(args.value(QStringLiteral("max_chars")).toInt(config.fetch_max_chars),
                          config.fetch_max_chars);
    const QUrl url(url_text);
    QString validation_error;
    if (is_web_fetch_url_allowed(url, config.allow_private_fetch_hosts, &validation_error) ==
        false)
    {
        return QStringLiteral("Error: %1").arg(validation_error);
    }

    const http_response_t response = perform_network_request(
        QStringLiteral("GET"), url, {},
        {{QByteArrayLiteral("Accept"),
          QByteArrayLiteral("text/html,application/xhtml+xml,application/json,text/plain,text/"
                            "markdown;q=0.9,*/*;q=0.1")}},
        request_timeout_ms(config));
    if (response.error.isEmpty() == false)
    {
        return QStringLiteral("Error: %1").arg(response.error);
    }

    const QString content_type = normalize_content_type(response.content_type);
    if (raw == false && is_html_content_type(content_type) == false &&
        is_textual_content_type(content_type) == false && content_type.isEmpty() == false)
    {
        return QStringLiteral("Error: Unsupported fetched content type: %1").arg(content_type);
    }

    const QString body_text = body_to_text(response.body, content_type, raw);
    const QString readable_text = normalize_whitespace(body_text);
    if (readable_text.isEmpty() == true)
    {
        return QStringLiteral("Error: The fetched response did not contain readable text.");
    }

    const bool truncated = readable_text.size() > max_chars;
    const QString limited_text = readable_text.left(max_chars).trimmed();
    const QString title = raw == false && is_html_content_type(content_type) == true
                              ? html_title(QString::fromUtf8(response.body))
                              : QString();
    const QString final_url =
        response.final_url.isEmpty() == false ? response.final_url : url.toString();
    return format_web_fetch_result(final_url, title, limited_text, content_type, truncated);
}

}  // namespace qcai2
