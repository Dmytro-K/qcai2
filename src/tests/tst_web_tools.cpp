/*! Unit tests for web_search/web_fetch helper logic. */

#include <QtTest>

#include "src/util/web_tools.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

using namespace qcai2;

namespace
{

struct http_test_response_t
{
    int status_code = 200;
    QByteArray content_type = QByteArrayLiteral("application/json");
    QByteArray body = QByteArrayLiteral("{}");
};

class http_test_server_t : public QObject
{
public:
    explicit http_test_server_t(std::function<http_test_response_t(const QByteArray &)> handler,
                                QObject *parent = nullptr)
        : QObject(parent), handler(std::move(handler))
    {
        QObject::connect(&this->server, &QTcpServer::newConnection, this,
                         [this]() { this->handle_connection(); });
        const bool listening = this->server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(listening);
    }

    quint16 port() const
    {
        return this->server.serverPort();
    }

    QByteArray last_request;

private:
    void handle_connection()
    {
        QTcpSocket *socket = this->server.nextPendingConnection();
        QVERIFY(socket != nullptr);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            this->last_request = socket->readAll();
            const http_test_response_t response = this->handler(this->last_request);
            const QByteArray status_text =
                response.status_code >= 200 && response.status_code < 300 ? "OK" : "Error";
            const QByteArray payload =
                QByteArray("HTTP/1.1 ") + QByteArray::number(response.status_code) + " " +
                status_text + "\r\nContent-Type: " + response.content_type +
                "\r\nConnection: close\r\nContent-Length: " +
                QByteArray::number(response.body.size()) + "\r\n\r\n" + response.body;
            socket->write(payload);
            socket->disconnectFromHost();
        });
    }

    QTcpServer server;
    std::function<http_test_response_t(const QByteArray &)> handler;
};

}  // namespace

class web_tools_test_t : public QObject
{
    Q_OBJECT

private slots:
    void default_web_search_endpoint_and_url_validation_cover_defaults_and_rejections();
    void url_validation_rejects_invalid_urls();
    void url_validation_allows_public_hosts();
    void url_validation_rejects_private_ipv4_ranges();
    void url_validation_rejects_private_host_names_and_ipv6_ranges();
    void html_helpers_cover_missing_title_and_list_formatting();
    void parse_serper_search_results_respects_max_results();
    void parse_perplexity_search_response_uses_root_fallbacks();
    void parse_brave_search_results_respects_max_results();
    void parse_duckduckgo_search_results_skips_empty_and_duplicate_primary_links();
    void format_perplexity_search_results_returns_no_results_for_empty_response();
    void parse_perplexity_search_response_handles_results_answers_and_citations();
    void parse_brave_search_results_handles_errors_and_extra_snippets();
    void parse_duckduckgo_search_results_extracts_title_url_and_snippet();
    void parse_duckduckgo_search_results_falls_back_to_lite_links_and_filters_duplicates();
    void parse_serper_search_results_handles_errors_and_answer_box_fallback();
    void format_helpers_cover_empty_and_truncated_cases();
    void execute_web_search_reads_perplexity_results();
    void execute_web_search_reads_brave_results();
    void execute_web_search_reads_serper_style_results();
    void execute_web_search_reads_duckduckgo_html_results();
    void execute_web_search_reports_configuration_errors();
    void execute_web_search_reports_http_errors_from_serper();
    void execute_web_search_reports_http_errors_from_perplexity();
    void execute_web_search_reports_http_errors_from_brave();
    void execute_web_search_reports_http_errors_from_duckduckgo();
    void execute_web_fetch_reports_http_errors();
    void execute_web_fetch_extracts_readable_html_text();
    void execute_web_fetch_formats_json_and_raw_results_and_reports_errors();
    void execute_web_fetch_rejects_private_hosts_by_default();
};

void web_tools_test_t::
    default_web_search_endpoint_and_url_validation_cover_defaults_and_rejections()
{
    const QString perplexity_endpoint = default_web_search_endpoint(QStringLiteral("perplexity"));
    const QString serper_endpoint = default_web_search_endpoint(QStringLiteral("serper"));
    const QString brave_endpoint = default_web_search_endpoint(QStringLiteral("brave"));
    const QString duckduckgo_endpoint = default_web_search_endpoint(QStringLiteral("duckduckgo"));
    QCOMPARE(perplexity_endpoint, QStringLiteral("https://api.perplexity.ai/chat/completions"));
    QCOMPARE(serper_endpoint, QStringLiteral("https://google.serper.dev/search"));
    QCOMPARE(brave_endpoint, QStringLiteral("https://api.search.brave.com/res/v1/web/search"));
    QCOMPARE(duckduckgo_endpoint, QStringLiteral("https://html.duckduckgo.com/html/"));

    QString error;
    const bool ftp_allowed =
        is_web_fetch_url_allowed(QUrl(QStringLiteral("ftp://example.com/file")), false, &error);
    QVERIFY(!ftp_allowed);
    QVERIFY(error.contains(QStringLiteral("Only http:// and https:// URLs are supported.")));

    const bool empty_host_allowed =
        is_web_fetch_url_allowed(QUrl(QStringLiteral("https:///missing-host")), false, &error);
    QVERIFY(!empty_host_allowed);
    QVERIFY(error.contains(QStringLiteral("URL host is empty.")));

    const bool localhost_allowed =
        is_web_fetch_url_allowed(QUrl(QStringLiteral("http://localhost/private")), false, &error);
    QVERIFY(!localhost_allowed);
    QVERIFY(error.contains(QStringLiteral("Private, local, or loopback hosts are not allowed.")));

    const bool private_override_allowed =
        is_web_fetch_url_allowed(QUrl(QStringLiteral("http://127.0.0.1/private")), true, &error);
    QVERIFY(private_override_allowed);
}

void web_tools_test_t::url_validation_rejects_invalid_urls()
{
    QString error;

    const bool invalid_allowed = is_web_fetch_url_allowed(QUrl(), false, &error);
    QVERIFY(!invalid_allowed);
    QVERIFY(error.contains(QStringLiteral("URL is invalid.")));
}

void web_tools_test_t::url_validation_allows_public_hosts()
{
    QString error;
    const bool public_host_allowed =
        is_web_fetch_url_allowed(QUrl(QStringLiteral("https://example.com/docs")), false, &error);
    QVERIFY(public_host_allowed);
}

void web_tools_test_t::url_validation_rejects_private_ipv4_ranges()
{
    QString error;
    const QStringList blocked_urls{
        QStringLiteral("http://10.0.0.1/private"),
        QStringLiteral("http://169.254.10.20/private"),
        QStringLiteral("http://172.16.0.1/private"),
        QStringLiteral("http://192.168.1.10/private"),
        QStringLiteral("http://0.0.0.0/private"),
    };
    for (const QString &url : blocked_urls)
    {
        QVERIFY(!is_web_fetch_url_allowed(QUrl(url), false, &error));
        QVERIFY(
            error.contains(QStringLiteral("Private, local, or loopback hosts are not allowed.")));
    }
}

void web_tools_test_t::url_validation_rejects_private_host_names_and_ipv6_ranges()
{
    QString error;
    const QStringList blocked_urls{
        QStringLiteral("http://[fd00::1]/private"),
        QStringLiteral("http://[fe80::1]/private"),
        QStringLiteral("http://service.internal/private"),
        QStringLiteral("http://printer.home.arpa/private"),
    };
    for (const QString &url : blocked_urls)
    {
        QVERIFY(!is_web_fetch_url_allowed(QUrl(url), false, &error));
        QVERIFY(
            error.contains(QStringLiteral("Private, local, or loopback hosts are not allowed.")));
    }
}

void web_tools_test_t::html_helpers_cover_missing_title_and_list_formatting()
{
    QVERIFY(html_title(QStringLiteral("<html><body>no title here</body></html>")).isEmpty());
    QVERIFY(
        html_to_readable_text(
            QStringLiteral("<div>alpha</div><br><noscript>x</noscript><svg>y</svg><li>beta</li>"))
            .contains(QStringLiteral("- beta")));
}

void web_tools_test_t::parse_serper_search_results_respects_max_results()
{
    QString error;

    const QList<web_search_result_t> limited_serper = parse_serper_search_results(
        QByteArrayLiteral(
            "{\"organic\":["
            "{\"title\":\"First\",\"link\":\"https://example.com/1\",\"snippet\":\"one\"},"
            "{\"title\":\"Second\",\"link\":\"https://example.com/2\",\"snippet\":\"two\"}]}"),
        1, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(limited_serper.size(), 1);
    QCOMPARE(limited_serper.constFirst().title, QStringLiteral("First"));
}

void web_tools_test_t::parse_perplexity_search_response_uses_root_fallbacks()
{
    QString error;
    const perplexity_search_response_t root_fallback_response = parse_perplexity_search_response(
        QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"content\":\"Answer\",\"citations\":\"ignored\","
            "\"search_results\":[]}}],"
            "\"citations\":[\"https://cite.example/root\"],"
            "\"search_results\":["
            "{\"title\":\"\",\"url\":\"https://example.com/a\",\"content\":[\"content "
            "fallback\"]},"
            "{\"title\":\"Skip\",\"url\":\"\",\"snippet\":\"missing url\"},"
            "{\"title\":\"Second\",\"url\":\"https://example.com/b\",\"snippet\":\"second\"}]}"),
        1, &error);
    QVERIFY(error.isEmpty());
    const QStringList expected_citations{QStringLiteral("https://cite.example/root")};
    QCOMPARE(root_fallback_response.citations, expected_citations);
    QCOMPARE(root_fallback_response.results.size(), 1);
    QCOMPARE(root_fallback_response.results.constFirst().title,
             QStringLiteral("https://example.com/a"));
    QCOMPARE(root_fallback_response.results.constFirst().snippet,
             QStringLiteral("content fallback"));
}

void web_tools_test_t::parse_brave_search_results_respects_max_results()
{
    QString error;
    const QList<web_search_result_t> limited_brave = parse_brave_search_results(
        QByteArrayLiteral(
            "{\"web\":{\"results\":["
            "{\"title\":\"One\",\"url\":\"https://example.com/1\",\"description\":\"first\"},"
            "{\"title\":\"Two\",\"url\":\"https://example.com/"
            "2\",\"extra_snippets\":[\"second\"]}]}}"),
        1, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(limited_brave.size(), 1);
    QCOMPARE(limited_brave.constFirst().title, QStringLiteral("One"));
}

void web_tools_test_t::parse_duckduckgo_search_results_skips_empty_and_duplicate_primary_links()
{
    const QString duck_html = QStringLiteral(
        "<div class=\"result\">"
        "<a class=\"result__a\" href=\"\">Missing URL</a>"
        "<a class=\"result__a\" href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fdup\">Duplicate</a>"
        "<a class=\"result__a\" href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fdup\">Duplicate "
        "Again</a>"
        "</div>"
        "<a href=\"\">Empty href</a>");
    const QList<web_search_result_t> duck_results = parse_duckduckgo_search_results(duck_html, 5);
    QCOMPARE(duck_results.size(), 1);
    QCOMPARE(duck_results.constFirst().url, QStringLiteral("https://example.com/dup"));
    QVERIFY(duck_results.constFirst().snippet.isEmpty());
}

void web_tools_test_t::format_perplexity_search_results_returns_no_results_for_empty_response()
{
    const perplexity_search_response_t empty_perplexity;
    QCOMPARE(format_perplexity_search_results(empty_perplexity),
             QStringLiteral("No web results found."));
}

void web_tools_test_t::parse_perplexity_search_response_handles_results_answers_and_citations()
{
    QString error;
    const perplexity_search_response_t invalid_response =
        parse_perplexity_search_response("not-json", 3, &error);
    QVERIFY(invalid_response.answer.isEmpty());
    QVERIFY(error.contains(QStringLiteral("Invalid Perplexity response JSON")));

    error.clear();
    const perplexity_search_response_t wrong_root =
        parse_perplexity_search_response(QByteArrayLiteral("[]"), 3, &error);
    QVERIFY(wrong_root.answer.isEmpty());
    QVERIFY(error.contains(QStringLiteral("root must be a JSON object")));

    error.clear();
    const perplexity_search_response_t response = parse_perplexity_search_response(
        QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"content\":\"Grounded answer\","
            "\"citations\":[\"https://cite.example/1\",\"https://cite.example/2\"],"
            "\"search_results\":[{\"title\":\"Perplexity "
            "Result\",\"url\":\"https://example.com/r1\","
            "\"snippet\":\"Snippet\"},{\"title\":\"\",\"url\":\"https://example.com/r2\","
            "\"description\":\"Fallback description\"}]}}]}"),
        5, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(response.answer, QStringLiteral("Grounded answer"));
    QCOMPARE(response.results.size(), 2);
    QCOMPARE(response.results.constFirst().title, QStringLiteral("Perplexity Result"));
    QCOMPARE(response.results.constFirst().url, QStringLiteral("https://example.com/r1"));
    QCOMPARE(response.results.constFirst().snippet, QStringLiteral("Snippet"));
    QCOMPARE(response.results.at(1).title, QStringLiteral("https://example.com/r2"));
    QCOMPARE(response.results.at(1).snippet, QStringLiteral("Fallback description"));
    QCOMPARE(response.citations.size(), 2);

    const QString formatted = format_perplexity_search_results(response);
    QVERIFY(formatted.contains(QStringLiteral("Perplexity answer:")));
    QVERIFY(formatted.contains(QStringLiteral("Grounded answer")));
    QVERIFY(formatted.contains(QStringLiteral("Search results:")));
    QVERIFY(formatted.contains(QStringLiteral("[Perplexity Result](https://example.com/r1)")));
    QVERIFY(formatted.contains(QStringLiteral("Citations:")));
    QVERIFY(formatted.contains(QStringLiteral("https://cite.example/1")));
}

void web_tools_test_t::parse_brave_search_results_handles_errors_and_extra_snippets()
{
    QString error;
    QVERIFY(parse_brave_search_results("not-json", 3, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("Invalid Brave Search response JSON")));

    error.clear();
    QVERIFY(parse_brave_search_results(QByteArrayLiteral("[]"), 3, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("root must be a JSON object")));

    error.clear();
    const QList<web_search_result_t> results = parse_brave_search_results(
        QByteArrayLiteral("{\"web\":{\"results\":["
                          "{\"title\":\"Brave Docs\",\"url\":\"https://example.com/docs\","
                          "\"description\":\"\",\"extra_snippets\":[\"Extra context\"]},"
                          "{\"title\":\"Ignored\",\"url\":\"\",\"description\":\"skip me\"}]}}"),
        3, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().title, QStringLiteral("Brave Docs"));
    QCOMPARE(results.constFirst().url, QStringLiteral("https://example.com/docs"));
    QCOMPARE(results.constFirst().snippet, QStringLiteral("Extra context"));
}

void web_tools_test_t::parse_duckduckgo_search_results_extracts_title_url_and_snippet()
{
    const QString html = QStringLiteral(R"(
        <div class="result">
          <a class="result__a" href="/l/?uddg=https%3A%2F%2Fexample.com%2Fdocs">Example &amp; Title</a>
          <div class="result__snippet">Useful <b>snippet</b> text.</div>
        </div>)");

    const QList<web_search_result_t> results = parse_duckduckgo_search_results(html, 5);

    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().title, QStringLiteral("Example & Title"));
    QCOMPARE(results.constFirst().url, QStringLiteral("https://example.com/docs"));
    QCOMPARE(results.constFirst().snippet, QStringLiteral("Useful snippet text."));
}

void web_tools_test_t::
    parse_duckduckgo_search_results_falls_back_to_lite_links_and_filters_duplicates()
{
    const QString html = QStringLiteral(
        "<a href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fdocs\">Example docs</a>"
        "<a href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fdocs\">Example docs duplicate</a>"
        "<a href=\"https://duckduckgo.com/?q=ignored\">Internal search</a>");

    const QList<web_search_result_t> results = parse_duckduckgo_search_results(html, 5);

    QCOMPARE(results.size(), 1);
    QCOMPARE(results.constFirst().title, QStringLiteral("Example docs"));
    QCOMPARE(results.constFirst().url, QStringLiteral("https://example.com/docs"));
    QVERIFY(results.constFirst().snippet.isEmpty());
}

void web_tools_test_t::parse_serper_search_results_handles_errors_and_answer_box_fallback()
{
    QString error;
    QVERIFY(parse_serper_search_results("not-json", 3, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("Invalid Serper response JSON")));

    error.clear();
    QVERIFY(parse_serper_search_results(QByteArrayLiteral("[]"), 3, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("root must be a JSON object")));

    error.clear();
    const QList<web_search_result_t> answer_box_results = parse_serper_search_results(
        QByteArrayLiteral(
            "{\"organic\":[{\"title\":\"\",\"link\":\"https://invalid.example\","
            "\"snippet\":\"skip me\"}],"
            "\"answerBox\":{\"title\":\"Instant Answer\",\"link\":\"https://answer.example\","
            "\"snippet\":\"Direct answer\"}}"),
        3, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(answer_box_results.size(), 1);
    QCOMPARE(answer_box_results.constFirst().title, QStringLiteral("Instant Answer"));
    QCOMPARE(answer_box_results.constFirst().url, QStringLiteral("https://answer.example"));
    QCOMPARE(answer_box_results.constFirst().snippet, QStringLiteral("Direct answer"));
}

void web_tools_test_t::format_helpers_cover_empty_and_truncated_cases()
{
    const QList<web_search_result_t> empty_results;
    QCOMPARE(format_web_search_results(empty_results), QStringLiteral("No web results found."));

    const QString fetch_result =
        format_web_fetch_result(QStringLiteral("https://example.com"), QString(),
                                QStringLiteral("Body"), QStringLiteral("application/json"), true);
    QVERIFY(fetch_result.contains(QStringLiteral("URL: https://example.com")));
    QVERIFY(fetch_result.contains(QStringLiteral("Content-Type: application/json")));
    QVERIFY(fetch_result.contains(QStringLiteral("Body")));
    QVERIFY(fetch_result.contains(QStringLiteral("[truncated to configured fetch limit]")));
}

void web_tools_test_t::execute_web_search_reads_serper_style_results()
{
    http_test_server_t server([](const QByteArray &) {
        return http_test_response_t{
            200,
            QByteArrayLiteral("application/json"),
            QByteArrayLiteral(
                "{\"organic\":[{\"title\":\"Qt Creator\",\"link\":"
                "\"https://doc.qt.io/qtcreator/\",\"snippet\":\"IDE documentation\"}]}"),
        };
    });

    web_tools_config_t config;
    config.enabled = true;
    config.search_provider = QStringLiteral("serper");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/search").arg(server.port());
    config.search_api_key = QStringLiteral("secret");
    config.request_timeout_sec = 2;

    const QString result = execute_web_search(
        config, QJsonObject{{QStringLiteral("query"), QStringLiteral("qt creator")},
                            {QStringLiteral("max_results"), 1}});

    QVERIFY(server.last_request.contains("POST /search "));
    QVERIFY(server.last_request.toLower().contains("x-api-key: secret"));
    QVERIFY(result.contains(QStringLiteral("[Qt Creator](https://doc.qt.io/qtcreator/)")));
    QVERIFY(result.contains(QStringLiteral("IDE documentation")));
}

void web_tools_test_t::execute_web_search_reads_perplexity_results()
{
    http_test_server_t server([](const QByteArray &) {
        return http_test_response_t{
            200,
            QByteArrayLiteral("application/json"),
            QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"Perplexity summary\","
                              "\"citations\":[\"https://cite.example/1\"],"
                              "\"search_results\":[{\"title\":\"Perplexity "
                              "Doc\",\"url\":\"https://example.com/p\","
                              "\"snippet\":\"Research-backed snippet\"}]}}]}"),
        };
    });

    web_tools_config_t config;
    config.enabled = true;
    config.search_provider = QStringLiteral("perplexity");
    config.search_endpoint =
        QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(server.port());
    config.search_api_key = QStringLiteral("perplexity-secret");
    config.request_timeout_sec = 2;

    const QString result = execute_web_search(
        config, QJsonObject{{QStringLiteral("query"), QStringLiteral("perplexity api")},
                            {QStringLiteral("max_results"), 1}});

    QVERIFY(server.last_request.contains("POST /chat/completions "));
    QVERIFY(server.last_request.toLower().contains("authorization: bearer perplexity-secret"));
    QVERIFY(server.last_request.contains("\"model\":\"sonar\""));
    QVERIFY(result.contains(QStringLiteral("Perplexity answer:")));
    QVERIFY(result.contains(QStringLiteral("Perplexity summary")));
    QVERIFY(result.contains(QStringLiteral("[Perplexity Doc](https://example.com/p)")));
    QVERIFY(result.contains(QStringLiteral("Research-backed snippet")));
    QVERIFY(result.contains(QStringLiteral("https://cite.example/1")));
}

void web_tools_test_t::execute_web_search_reads_brave_results()
{
    http_test_server_t server([](const QByteArray &) {
        return http_test_response_t{
            200,
            QByteArrayLiteral("application/json"),
            QByteArrayLiteral(
                "{\"web\":{\"results\":[{\"title\":\"Brave Result\",\"url\":"
                "\"https://example.com/brave\",\"description\":\"Independent index\"}]}}"),
        };
    });

    web_tools_config_t config;
    config.enabled = true;
    config.search_provider = QStringLiteral("brave");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/search").arg(server.port());
    config.search_api_key = QStringLiteral("brave-secret");
    config.request_timeout_sec = 2;

    const QString result = execute_web_search(
        config, QJsonObject{{QStringLiteral("query"), QStringLiteral("brave api")},
                            {QStringLiteral("max_results"), 1}});

    QVERIFY(server.last_request.contains("GET /search?q=brave"));
    QVERIFY(server.last_request.contains("count=1"));
    QVERIFY(server.last_request.toLower().contains("x-subscription-token: brave-secret"));
    QVERIFY(result.contains(QStringLiteral("[Brave Result](https://example.com/brave)")));
    QVERIFY(result.contains(QStringLiteral("Independent index")));
}

void web_tools_test_t::execute_web_search_reads_duckduckgo_html_results()
{
    http_test_server_t server([](const QByteArray &) {
        return http_test_response_t{
            200,
            QByteArrayLiteral("text/html; charset=utf-8"),
            QByteArrayLiteral("<div class=\"result\"><a class=\"result__a\" "
                              "href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fguide\">Guide</a>"
                              "<div class=\"result__snippet\">Helpful docs</div></div>"),
        };
    });

    web_tools_config_t config;
    config.enabled = true;
    config.search_provider = QStringLiteral("duckduckgo");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/html").arg(server.port());
    config.request_timeout_sec = 2;

    const QString result = execute_web_search(
        config, QJsonObject{{QStringLiteral("query"), QStringLiteral("guide query")},
                            {QStringLiteral("max_results"), 1}});

    QVERIFY(server.last_request.contains("GET /html?q=guide"));
    QVERIFY(server.last_request.contains("query "));
    QVERIFY(result.contains(QStringLiteral("[Guide](https://example.com/guide)")));
    QVERIFY(result.contains(QStringLiteral("Helpful docs")));
}

void web_tools_test_t::execute_web_search_reports_configuration_errors()
{
    web_tools_config_t config;
    const QJsonObject query_args{{QStringLiteral("query"), QStringLiteral("qt")}};

    QCOMPARE(execute_web_search(config, query_args),
             QStringLiteral("Error: Web tools are disabled. Enable them in Settings > Qcai2 > "
                            "Web Tools."));

    config.enabled = true;
    QCOMPARE(execute_web_search(config, QJsonObject{}),
             QStringLiteral("Error: 'query' argument is required."));

    config.search_endpoint = QStringLiteral("notaurl");
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("valid http:// or https:// URL")));

    config.search_provider = QStringLiteral("serper");
    config.search_endpoint = QStringLiteral("https://google.serper.dev/search");
    config.search_api_key.clear();
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("requires a configured API key")));

    config.search_provider = QStringLiteral("perplexity");
    config.search_endpoint = QStringLiteral("https://api.perplexity.ai/chat/completions");
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("Perplexity provider requires a configured API key")));

    config.search_provider = QStringLiteral("brave");
    config.search_endpoint = QStringLiteral("https://api.search.brave.com/res/v1/web/search");
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("Brave Search provider requires a configured API key")));

    http_test_server_t bad_serper([](const QByteArray &) {
        return http_test_response_t{200, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("not-json")};
    });
    config.search_provider = QStringLiteral("serper");
    config.search_api_key = QStringLiteral("secret");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/search").arg(bad_serper.port());
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("Invalid Serper response JSON")));

    http_test_server_t bad_brave([](const QByteArray &) {
        return http_test_response_t{200, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("not-json")};
    });
    config.search_provider = QStringLiteral("brave");
    config.search_api_key = QStringLiteral("brave-secret");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/search").arg(bad_brave.port());
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("Invalid Brave Search response JSON")));

    http_test_server_t bad_perplexity([](const QByteArray &) {
        return http_test_response_t{200, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("not-json")};
    });
    config.search_provider = QStringLiteral("perplexity");
    config.search_api_key = QStringLiteral("perplexity-secret");
    config.search_endpoint =
        QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(bad_perplexity.port());
    QVERIFY(execute_web_search(config, query_args)
                .contains(QStringLiteral("Invalid Perplexity response JSON")));
}

void web_tools_test_t::execute_web_search_reports_http_errors_from_serper()
{
    http_test_server_t error_server([](const QByteArray &) {
        return http_test_response_t{500, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("{\"error\":\"boom\"}")};
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;
    const QJsonObject query_args{{QStringLiteral("query"), QStringLiteral("qt")}};

    config.search_provider = QStringLiteral("serper");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/search").arg(error_server.port());
    config.search_api_key = QStringLiteral("secret");
    QVERIFY(execute_web_search(config, query_args).contains(QStringLiteral("boom")));
}

void web_tools_test_t::execute_web_search_reports_http_errors_from_perplexity()
{
    http_test_server_t error_server([](const QByteArray &) {
        return http_test_response_t{500, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("{\"error\":\"boom\"}")};
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;
    const QJsonObject query_args{{QStringLiteral("query"), QStringLiteral("qt")}};
    config.search_provider = QStringLiteral("perplexity");
    config.search_endpoint =
        QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(error_server.port());
    config.search_api_key = QStringLiteral("perplexity-secret");
    QVERIFY(execute_web_search(config, query_args).contains(QStringLiteral("boom")));
}

void web_tools_test_t::execute_web_search_reports_http_errors_from_brave()
{
    http_test_server_t error_server([](const QByteArray &) {
        return http_test_response_t{500, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("{\"error\":\"boom\"}")};
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;
    const QJsonObject query_args{{QStringLiteral("query"), QStringLiteral("qt")}};
    config.search_provider = QStringLiteral("brave");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/search").arg(error_server.port());
    config.search_api_key = QStringLiteral("brave-secret");
    QVERIFY(execute_web_search(config, query_args).contains(QStringLiteral("boom")));
}

void web_tools_test_t::execute_web_search_reports_http_errors_from_duckduckgo()
{
    http_test_server_t error_server([](const QByteArray &) {
        return http_test_response_t{500, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("{\"error\":\"boom\"}")};
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;
    const QJsonObject query_args{{QStringLiteral("query"), QStringLiteral("qt")}};
    config.search_provider = QStringLiteral("duckduckgo");
    config.search_endpoint = QStringLiteral("http://127.0.0.1:%1/html").arg(error_server.port());
    config.search_api_key.clear();
    QVERIFY(execute_web_search(config, query_args).contains(QStringLiteral("boom")));
}

void web_tools_test_t::execute_web_fetch_reports_http_errors()
{
    http_test_server_t error_server([](const QByteArray &) {
        return http_test_response_t{500, QByteArrayLiteral("application/json"),
                                    QByteArrayLiteral("{\"error\":\"boom\"}")};
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;
    const QJsonObject fetch_args{
        {QStringLiteral("url"),
         QStringLiteral("http://127.0.0.1:%1/fetch").arg(error_server.port())}};
    const QString fetch_result = execute_web_fetch(config, fetch_args);
    QVERIFY(fetch_result.contains(QStringLiteral("HTTP request failed")));
    QVERIFY(fetch_result.contains(QStringLiteral("boom")));
}

void web_tools_test_t::execute_web_fetch_extracts_readable_html_text()
{
    http_test_server_t server([](const QByteArray &) {
        return http_test_response_t{
            200,
            QByteArrayLiteral("text/html; charset=utf-8"),
            QByteArrayLiteral(
                "<html><head><title>Example page</title><script>ignored()</script></head>"
                "<body><main><h1>Hello world</h1><p>Readable text.</p><ul><li>First</li></ul>"
                "</main></body></html>"),
        };
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;

    const QString result = execute_web_fetch(
        config, QJsonObject{{QStringLiteral("url"),
                             QStringLiteral("http://127.0.0.1:%1/page").arg(server.port())}});

    QVERIFY(result.contains(QStringLiteral("# Example page")));
    QVERIFY(result.contains(QStringLiteral("Hello world")));
    QVERIFY(result.contains(QStringLiteral("Readable text.")));
    QVERIFY(result.contains(QStringLiteral("- First")));
    QVERIFY(result.contains(QStringLiteral("URL: http://127.0.0.1")));
    QVERIFY(result.contains(QStringLiteral("Content-Type: text/html")));
    QVERIFY(!result.contains(QStringLiteral("ignored()")));
}

void web_tools_test_t::execute_web_fetch_formats_json_and_raw_results_and_reports_errors()
{
    http_test_server_t server([](const QByteArray &request) {
        if (request.contains("GET /json "))
        {
            return http_test_response_t{200, QByteArrayLiteral("application/json"),
                                        QByteArrayLiteral("{\"key\":\"value\"}")};
        }
        if (request.contains("GET /binary "))
        {
            return http_test_response_t{200, QByteArrayLiteral("application/octet-stream"),
                                        QByteArray(700, 'R')};
        }
        if (request.contains("GET /empty "))
        {
            return http_test_response_t{200, QByteArrayLiteral("text/plain"),
                                        QByteArrayLiteral("")};
        }
        return http_test_response_t{404, QByteArrayLiteral("text/plain"),
                                    QByteArrayLiteral("not found")};
    });

    web_tools_config_t config;
    config.enabled = true;
    config.allow_private_fetch_hosts = true;
    config.request_timeout_sec = 2;

    const QString json_result = execute_web_fetch(
        config, QJsonObject{{QStringLiteral("url"),
                             QStringLiteral("http://127.0.0.1:%1/json").arg(server.port())}});
    QVERIFY(json_result.contains(QStringLiteral("Content-Type: application/json")));
    QVERIFY(json_result.contains(QStringLiteral("\"key\": \"value\"")));

    const QString unsupported_result = execute_web_fetch(
        config, QJsonObject{{QStringLiteral("url"),
                             QStringLiteral("http://127.0.0.1:%1/binary").arg(server.port())}});
    QVERIFY(unsupported_result.contains(QStringLiteral("Unsupported fetched content type")));

    const QString raw_result = execute_web_fetch(
        config, QJsonObject{{QStringLiteral("url"),
                             QStringLiteral("http://127.0.0.1:%1/binary").arg(server.port())},
                            {QStringLiteral("raw"), true},
                            {QStringLiteral("max_chars"), 512}});
    QVERIFY(raw_result.contains(QString(64, QLatin1Char('R'))));
    QVERIFY(raw_result.contains(QStringLiteral("[truncated to configured fetch limit]")));

    const QString empty_result = execute_web_fetch(
        config, QJsonObject{{QStringLiteral("url"),
                             QStringLiteral("http://127.0.0.1:%1/empty").arg(server.port())}});
    QVERIFY(empty_result.contains(QStringLiteral("did not contain readable text")));

    QCOMPARE(execute_web_fetch(config, QJsonObject{}),
             QStringLiteral("Error: 'url' argument is required."));

    config.enabled = false;
    const QJsonObject disabled_fetch_args{
        {QStringLiteral("url"), QStringLiteral("https://example.com")}};
    QVERIFY(execute_web_fetch(config, disabled_fetch_args)
                .contains(QStringLiteral("Web tools are disabled")));
}

void web_tools_test_t::execute_web_fetch_rejects_private_hosts_by_default()
{
    web_tools_config_t config;
    config.enabled = true;

    const QString result = execute_web_fetch(
        config, QJsonObject{{QStringLiteral("url"), QStringLiteral("http://127.0.0.1/private")}});

    QVERIFY(result.contains(QStringLiteral("Error:")));
    QVERIFY(result.contains(QStringLiteral("Private, local, or loopback hosts are not allowed.")));
}

QTEST_MAIN(web_tools_test_t)

#include "tst_web_tools.moc"
