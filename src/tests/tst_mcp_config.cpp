/*! Unit tests for reusable MCP server definition JSON serialization. */

#include <QtTest>

#include <qtmcp/server_definition.h>

#include <QJsonObject>

using namespace qtmcp;

class mcp_config_test_t : public QObject
{
    Q_OBJECT

private slots:
    void server_definitions_round_trip_preserves_known_and_extra_fields();
    void server_definition_from_json_keeps_defaults_when_optional_fields_are_missing();
    void server_definition_from_json_accepts_zero_timeouts_as_unlimited();
    void server_definition_from_json_rejects_invalid_field_types();
    void server_definitions_from_json_reports_server_specific_errors();
};

void mcp_config_test_t::server_definitions_round_trip_preserves_known_and_extra_fields()
{
    server_definitions_t definitions;

    server_definition_t filesystem;
    filesystem.enabled = true;
    filesystem.transport = QStringLiteral("stdio");
    filesystem.command = QStringLiteral("npx");
    filesystem.args = {QStringLiteral("-y"),
                       QStringLiteral("@modelcontextprotocol/server-filesystem"),
                       QStringLiteral("/tmp/project")};
    filesystem.env.insert(QStringLiteral("NODE_ENV"), QStringLiteral("production"));
    filesystem.startup_timeout_ms = 12000;
    filesystem.request_timeout_ms = 45000;
    filesystem.extra_fields.insert(QStringLiteral("notes"), QStringLiteral("preserve-me"));
    definitions.insert(QStringLiteral("filesystem"), filesystem);

    server_definition_t github;
    github.enabled = true;
    github.transport = QStringLiteral("http");
    github.url = QStringLiteral("https://api.githubcopilot.com/mcp/");
    github.headers.insert(QStringLiteral("Authorization"),
                          QStringLiteral("Bearer ${GITHUB_TOKEN}"));
    github.request_timeout_ms = 30000;
    github.extra_fields.insert(QStringLiteral("customTransportFlag"), true);
    definitions.insert(QStringLiteral("github"), github);

    const QJsonObject json = server_definitions_to_json(definitions);

    server_definitions_t parsed;
    QString error;
    QVERIFY2(server_definitions_from_json(json, &parsed, &error), qPrintable(error));

    QCOMPARE(parsed.keys(), definitions.keys());
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).command, filesystem.command);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).args, filesystem.args);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).env, filesystem.env);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).startup_timeout_ms,
             filesystem.startup_timeout_ms);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).request_timeout_ms,
             filesystem.request_timeout_ms);
    QCOMPARE(parsed.value(QStringLiteral("filesystem"))
                 .extra_fields.value(QStringLiteral("notes"))
                 .toString(),
             QStringLiteral("preserve-me"));

    QCOMPARE(parsed.value(QStringLiteral("github")).transport, QStringLiteral("http"));
    QCOMPARE(parsed.value(QStringLiteral("github")).url, github.url);
    QCOMPARE(parsed.value(QStringLiteral("github")).headers, github.headers);
    QCOMPARE(parsed.value(QStringLiteral("github")).request_timeout_ms, github.request_timeout_ms);
    QCOMPARE(parsed.value(QStringLiteral("github"))
                 .extra_fields.value(QStringLiteral("customTransportFlag"))
                 .toBool(),
             true);
}

void mcp_config_test_t::
    server_definition_from_json_keeps_defaults_when_optional_fields_are_missing()
{
    const QJsonObject json{{QStringLiteral("command"), QStringLiteral("npx")}};

    server_definition_t definition;
    QString error;
    QVERIFY2(server_definition_from_json(json, &definition, &error), qPrintable(error));

    QVERIFY(definition.enabled);
    QCOMPARE(definition.transport, QStringLiteral("stdio"));
    QCOMPARE(definition.command, QStringLiteral("npx"));
    QCOMPARE(definition.startup_timeout_ms, 10000);
    QCOMPARE(definition.request_timeout_ms, 30000);
    QVERIFY(definition.args.isEmpty());
    QVERIFY(definition.env.isEmpty());
    QVERIFY(definition.headers.isEmpty());
}

void mcp_config_test_t::server_definition_from_json_accepts_zero_timeouts_as_unlimited()
{
    const QJsonObject json{{QStringLiteral("startupTimeoutMs"), 0},
                           {QStringLiteral("requestTimeoutMs"), 0}};

    server_definition_t definition;
    QString error;
    QVERIFY2(server_definition_from_json(json, &definition, &error), qPrintable(error));

    QCOMPARE(definition.startup_timeout_ms, 0);
    QCOMPARE(definition.request_timeout_ms, 0);
}

void mcp_config_test_t::server_definition_from_json_rejects_invalid_field_types()
{
    server_definition_t definition;
    QString error;

    QVERIFY(!server_definition_from_json(
        QJsonObject{{QStringLiteral("enabled"), QStringLiteral("yes")}}, &definition, &error));
    QCOMPARE(error, QStringLiteral("Field 'enabled' must be a boolean."));

    QVERIFY(!server_definition_from_json(
        QJsonObject{{QStringLiteral("args"), QJsonArray{QStringLiteral("ok"), 3}}}, &definition,
        &error));
    QCOMPARE(error, QStringLiteral("Field 'args[1]' must be a string."));

    QVERIFY(!server_definition_from_json(
        QJsonObject{
            {QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), 42}}}},
        &definition, &error));
    QCOMPARE(error, QStringLiteral("Field 'headers.Authorization' must be a string."));
}

void mcp_config_test_t::server_definitions_from_json_reports_server_specific_errors()
{
    server_definitions_t definitions;
    QString error;

    QVERIFY(!server_definitions_from_json(
        QJsonObject{{QStringLiteral("broken"),
                     QJsonObject{{QStringLiteral("headers"),
                                  QJsonObject{{QStringLiteral("Authorization"), false}}}}}},
        &definitions, &error));
    QCOMPARE(error,
             QStringLiteral(
                 "Invalid MCP server 'broken': Field 'headers.Authorization' must be a string."));

    QVERIFY(!server_definitions_from_json(
        QJsonObject{{QStringLiteral("filesystem"), QStringLiteral("not-an-object")}}, &definitions,
        &error));
    QCOMPARE(error, QStringLiteral("MCP server 'filesystem' must be an object."));
}

QTEST_APPLESS_MAIN(mcp_config_test_t)

#include "tst_mcp_config.moc"
