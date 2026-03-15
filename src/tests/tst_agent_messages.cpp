/*! Unit tests for chat message and structured agent response parsing. */

#include <QtTest>

#include "src/models/AgentMessages.h"

#include <QJsonDocument>

using namespace qcai2;

class AgentMessagesTest : public QObject
{
    Q_OBJECT

private slots:
    void chatMessage_roundTripsThroughJson();
    void parse_planBuildsIndexedStepsFromStringsAndObjects();
    void parse_toolCallReadsNameAndArgs();
    void parse_needApprovalReadsApprovalFields();
    void parse_unknownJsonObjectFallsBackToCompactText();
    void parse_fencedJsonExtractsStructuredResponse();
    void parse_multipleObjectsHandlesBracesInsideStrings();
    void parse_plainTextFallsBackToRawText();
};

void AgentMessagesTest::chatMessage_roundTripsThroughJson()
{
    const ChatMessage source{QStringLiteral("assistant"),
                             QStringLiteral("Line 1\nLine 2 with \"quotes\"")};

    const QJsonObject json = source.toJson();
    const ChatMessage parsed = ChatMessage::fromJson(json);

    QCOMPARE(parsed.role, source.role);
    QCOMPARE(parsed.content, source.content);
}

void AgentMessagesTest::parse_planBuildsIndexedStepsFromStringsAndObjects()
{
    const AgentResponse response = AgentResponse::parse(
        QStringLiteral(R"({"type":"plan","steps":["Inspect code",{"description":"Add tests"}]})"));

    QVERIFY(response.type == ResponseType::Plan);
    QCOMPARE(response.steps.size(), 2);
    QCOMPARE(response.steps.at(0).index, 0);
    QCOMPARE(response.steps.at(0).description, QStringLiteral("Inspect code"));
    QCOMPARE(response.steps.at(1).index, 1);
    QCOMPARE(response.steps.at(1).description, QStringLiteral("Add tests"));
}

void AgentMessagesTest::parse_toolCallReadsNameAndArgs()
{
    const AgentResponse response = AgentResponse::parse(
        QStringLiteral(R"({"type":"tool_call","name":"search_repo","args":{"pattern":"TODO","path":"src"}})"));

    QVERIFY(response.type == ResponseType::ToolCall);
    QCOMPARE(response.toolName, QStringLiteral("search_repo"));
    QCOMPARE(response.toolArgs.value(QStringLiteral("pattern")).toString(), QStringLiteral("TODO"));
    QCOMPARE(response.toolArgs.value(QStringLiteral("path")).toString(), QStringLiteral("src"));
}

void AgentMessagesTest::parse_needApprovalReadsApprovalFields()
{
    const AgentResponse response = AgentResponse::parse(
        QStringLiteral(R"({"type":"need_approval","action":"apply_patch","reason":"Needs consent","preview":"diff"})"));

    QVERIFY(response.type == ResponseType::NeedApproval);
    QCOMPARE(response.approvalAction, QStringLiteral("apply_patch"));
    QCOMPARE(response.approvalReason, QStringLiteral("Needs consent"));
    QCOMPARE(response.approvalPreview, QStringLiteral("diff"));
}

void AgentMessagesTest::parse_unknownJsonObjectFallsBackToCompactText()
{
    const AgentResponse response =
        AgentResponse::parse(QStringLiteral(R"({"type":"mystery","answer":42})"));

    QVERIFY(response.type == ResponseType::Text);

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(response.text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("type")).toString(), QStringLiteral("mystery"));
    QCOMPARE(document.object().value(QStringLiteral("answer")).toInt(), 42);
}

void AgentMessagesTest::parse_fencedJsonExtractsStructuredResponse()
{
    const QString raw = QStringLiteral(
        "Before\n```json\n{\"type\":\"final\",\"summary\":\"Done\",\"diff\":\"--- a/file\"}\n```\nAfter");

    const AgentResponse response = AgentResponse::parse(raw);

    QVERIFY(response.type == ResponseType::Final);
    QCOMPARE(response.summary, QStringLiteral("Done"));
    QCOMPARE(response.diff, QStringLiteral("--- a/file"));
}

void AgentMessagesTest::parse_multipleObjectsHandlesBracesInsideStrings()
{
    const QString raw = QStringLiteral(
        "noise {\"type\":\"final\",\"summary\":\"brace { inside }\",\"diff\":\"\"}"
        "{\"type\":\"tool_call\",\"name\":\"ignored\",\"args\":{}} trailing");

    const AgentResponse response = AgentResponse::parse(raw);

    QVERIFY(response.type == ResponseType::Final);
    QCOMPARE(response.summary, QStringLiteral("brace { inside }"));
}

void AgentMessagesTest::parse_plainTextFallsBackToRawText()
{
    const QString raw = QStringLiteral("Just a normal assistant reply.");

    const AgentResponse response = AgentResponse::parse(raw);

    QVERIFY(response.type == ResponseType::Text);
    QCOMPARE(response.text, raw);
}

QTEST_APPLESS_MAIN(AgentMessagesTest)

#include "tst_agent_messages.moc"
