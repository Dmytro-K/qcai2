/*! Unit tests for reusable MCP server definition JSON serialization. */

#include <QtTest>

#include <qtmcp/ServerDefinition.h>

#include <QJsonObject>

using namespace qtmcp;

class McpConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void serverDefinitions_roundTripPreservesKnownAndExtraFields();
    void serverDefinitionFromJson_keepsDefaultsWhenOptionalFieldsAreMissing();
    void serverDefinitionFromJson_rejectsInvalidFieldTypes();
    void serverDefinitionsFromJson_reportsServerSpecificErrors();
};

void McpConfigTest::serverDefinitions_roundTripPreservesKnownAndExtraFields()
{
    ServerDefinitions definitions;

    ServerDefinition filesystem;
    filesystem.enabled = true;
    filesystem.transport = QStringLiteral("stdio");
    filesystem.command = QStringLiteral("npx");
    filesystem.args = {QStringLiteral("-y"), QStringLiteral("@modelcontextprotocol/server-filesystem"),
                       QStringLiteral("/tmp/project")};
    filesystem.env.insert(QStringLiteral("NODE_ENV"), QStringLiteral("production"));
    filesystem.startupTimeoutMs = 12000;
    filesystem.requestTimeoutMs = 45000;
    filesystem.extraFields.insert(QStringLiteral("notes"), QStringLiteral("preserve-me"));
    definitions.insert(QStringLiteral("filesystem"), filesystem);

    ServerDefinition github;
    github.enabled = true;
    github.transport = QStringLiteral("http");
    github.url = QStringLiteral("https://api.githubcopilot.com/mcp/");
    github.headers.insert(QStringLiteral("Authorization"),
                          QStringLiteral("Bearer ${GITHUB_TOKEN}"));
    github.requestTimeoutMs = 30000;
    github.extraFields.insert(QStringLiteral("customTransportFlag"), true);
    definitions.insert(QStringLiteral("github"), github);

    const QJsonObject json = serverDefinitionsToJson(definitions);

    ServerDefinitions parsed;
    QString error;
    QVERIFY2(serverDefinitionsFromJson(json, &parsed, &error), qPrintable(error));

    QCOMPARE(parsed.keys(), definitions.keys());
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).command, filesystem.command);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).args, filesystem.args);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).env, filesystem.env);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).startupTimeoutMs, filesystem.startupTimeoutMs);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).requestTimeoutMs, filesystem.requestTimeoutMs);
    QCOMPARE(parsed.value(QStringLiteral("filesystem")).extraFields.value(QStringLiteral("notes")).toString(),
             QStringLiteral("preserve-me"));

    QCOMPARE(parsed.value(QStringLiteral("github")).transport, QStringLiteral("http"));
    QCOMPARE(parsed.value(QStringLiteral("github")).url, github.url);
    QCOMPARE(parsed.value(QStringLiteral("github")).headers, github.headers);
    QCOMPARE(parsed.value(QStringLiteral("github")).requestTimeoutMs, github.requestTimeoutMs);
    QCOMPARE(parsed.value(QStringLiteral("github")).extraFields.value(QStringLiteral("customTransportFlag")).toBool(),
             true);
}

void McpConfigTest::serverDefinitionFromJson_keepsDefaultsWhenOptionalFieldsAreMissing()
{
    const QJsonObject json{{QStringLiteral("command"), QStringLiteral("npx")}};

    ServerDefinition definition;
    QString error;
    QVERIFY2(serverDefinitionFromJson(json, &definition, &error), qPrintable(error));

    QVERIFY(definition.enabled);
    QCOMPARE(definition.transport, QStringLiteral("stdio"));
    QCOMPARE(definition.command, QStringLiteral("npx"));
    QCOMPARE(definition.startupTimeoutMs, 10000);
    QCOMPARE(definition.requestTimeoutMs, 30000);
    QVERIFY(definition.args.isEmpty());
    QVERIFY(definition.env.isEmpty());
    QVERIFY(definition.headers.isEmpty());
}

void McpConfigTest::serverDefinitionFromJson_rejectsInvalidFieldTypes()
{
    ServerDefinition definition;
    QString error;

    QVERIFY(!serverDefinitionFromJson(QJsonObject{{QStringLiteral("enabled"), QStringLiteral("yes")}},
                                      &definition, &error));
    QCOMPARE(error, QStringLiteral("Field 'enabled' must be a boolean."));

    QVERIFY(!serverDefinitionFromJson(
        QJsonObject{{QStringLiteral("args"), QJsonArray{QStringLiteral("ok"), 3}}}, &definition,
        &error));
    QCOMPARE(error, QStringLiteral("Field 'args[1]' must be a string."));

    QVERIFY(!serverDefinitionFromJson(
        QJsonObject{{QStringLiteral("headers"),
                     QJsonObject{{QStringLiteral("Authorization"), 42}}}},
        &definition, &error));
    QCOMPARE(error, QStringLiteral("Field 'headers.Authorization' must be a string."));
}

void McpConfigTest::serverDefinitionsFromJson_reportsServerSpecificErrors()
{
    ServerDefinitions definitions;
    QString error;

    QVERIFY(!serverDefinitionsFromJson(
        QJsonObject{{QStringLiteral("broken"),
                     QJsonObject{{QStringLiteral("headers"),
                                  QJsonObject{{QStringLiteral("Authorization"), false}}}}}},
        &definitions, &error));
    QCOMPARE(error,
             QStringLiteral("Invalid MCP server 'broken': Field 'headers.Authorization' must be a string."));

    QVERIFY(!serverDefinitionsFromJson(
        QJsonObject{{QStringLiteral("filesystem"), QStringLiteral("not-an-object")}}, &definitions,
        &error));
    QCOMPARE(error, QStringLiteral("MCP server 'filesystem' must be an object."));
}

QTEST_APPLESS_MAIN(McpConfigTest)

#include "tst_mcp_config.moc"
