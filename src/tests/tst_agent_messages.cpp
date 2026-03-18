/*! Unit tests for chat message and structured agent response parsing. */

#include <QtTest>

#include "src/models/agent_messages.h"

#include <QJsonDocument>

using namespace qcai2;

class agent_messages_test_t : public QObject
{
    Q_OBJECT

private slots:
    void chat_message_round_trips_through_json();
    void parse_plan_builds_indexed_steps_from_strings_and_objects();
    void parse_tool_call_reads_name_and_args();
    void parse_need_approval_reads_approval_fields();
    void parse_unknown_json_object_falls_back_to_compact_text();
    void parse_fenced_json_extracts_structured_response();
    void parse_multiple_objects_handles_braces_inside_strings();
    void parse_plain_text_falls_back_to_raw_text();
};

void agent_messages_test_t::chat_message_round_trips_through_json()
{
    const chat_message_t source{QStringLiteral("assistant"),
                                QStringLiteral("Line 1\nLine 2 with \"quotes\"")};

    const QJsonObject json = source.to_json();
    const chat_message_t parsed = chat_message_t::from_json(json);

    QCOMPARE(parsed.role, source.role);
    QCOMPARE(parsed.content, source.content);
}

void agent_messages_test_t::parse_plan_builds_indexed_steps_from_strings_and_objects()
{
    const agent_response_t response = agent_response_t::parse(
        QStringLiteral(R"({"type":"plan","steps":["Inspect code",{"description":"Add tests"}]})"));

    QVERIFY(response.type == response_type_t::PLAN);
    QCOMPARE(response.steps.size(), 2);
    QCOMPARE(response.steps.at(0).index, 0);
    QCOMPARE(response.steps.at(0).description, QStringLiteral("Inspect code"));
    QCOMPARE(response.steps.at(1).index, 1);
    QCOMPARE(response.steps.at(1).description, QStringLiteral("Add tests"));
}

void agent_messages_test_t::parse_tool_call_reads_name_and_args()
{
    const agent_response_t response = agent_response_t::parse(QStringLiteral(
        R"({"type":"tool_call","name":"search_repo","args":{"pattern":"TODO","path":"src"}})"));

    QVERIFY(response.type == response_type_t::TOOL_CALL);
    QCOMPARE(response.tool_name, QStringLiteral("search_repo"));
    QCOMPARE(response.tool_args.value(QStringLiteral("pattern")).toString(),
             QStringLiteral("TODO"));
    QCOMPARE(response.tool_args.value(QStringLiteral("path")).toString(), QStringLiteral("src"));
}

void agent_messages_test_t::parse_need_approval_reads_approval_fields()
{
    const agent_response_t response = agent_response_t::parse(QStringLiteral(
        R"({"type":"need_approval","action":"apply_patch","reason":"Needs consent","preview":"diff"})"));

    QVERIFY(response.type == response_type_t::NEED_APPROVAL);
    QCOMPARE(response.approval_action, QStringLiteral("apply_patch"));
    QCOMPARE(response.approval_reason, QStringLiteral("Needs consent"));
    QCOMPARE(response.approval_preview, QStringLiteral("diff"));
}

void agent_messages_test_t::parse_unknown_json_object_falls_back_to_compact_text()
{
    const agent_response_t response =
        agent_response_t::parse(QStringLiteral(R"({"type":"mystery","answer":42})"));

    QVERIFY(response.type == response_type_t::TEXT);

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(response.text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("type")).toString(),
             QStringLiteral("mystery"));
    QCOMPARE(document.object().value(QStringLiteral("answer")).toInt(), 42);
}

void agent_messages_test_t::parse_fenced_json_extracts_structured_response()
{
    const QString raw = QStringLiteral("Before\n```json\n{\"type\":\"final\",\"summary\":\"Done\","
                                       "\"diff\":\"--- a/file\"}\n```\nAfter");

    const agent_response_t response = agent_response_t::parse(raw);

    QVERIFY(response.type == response_type_t::FINAL);
    QCOMPARE(response.summary, QStringLiteral("Done"));
    QCOMPARE(response.diff, QStringLiteral("--- a/file"));
}

void agent_messages_test_t::parse_multiple_objects_handles_braces_inside_strings()
{
    const QString raw =
        QStringLiteral("noise {\"type\":\"final\",\"summary\":\"brace { inside }\",\"diff\":\"\"}"
                       "{\"type\":\"tool_call\",\"name\":\"ignored\",\"args\":{}} trailing");

    const agent_response_t response = agent_response_t::parse(raw);

    QVERIFY(response.type == response_type_t::FINAL);
    QCOMPARE(response.summary, QStringLiteral("brace { inside }"));
}

void agent_messages_test_t::parse_plain_text_falls_back_to_raw_text()
{
    const QString raw = QStringLiteral("Just a normal assistant reply.");

    const agent_response_t response = agent_response_t::parse(raw);

    QVERIFY(response.type == response_type_t::TEXT);
    QCOMPARE(response.text, raw);
}

QTEST_APPLESS_MAIN(agent_messages_test_t)

#include "tst_agent_messages.moc"
