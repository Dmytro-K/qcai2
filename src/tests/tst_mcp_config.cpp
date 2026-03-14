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

QTEST_APPLESS_MAIN(McpConfigTest)

#include "tst_mcp_config.moc"
