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
    void chat_message_round_trips_image_attachments();
    void decision_request_round_trips_through_json_and_helpers();
    void parse_plan_builds_indexed_steps_from_strings_and_objects();
    void parse_tool_call_reads_name_and_args();
    void parse_need_approval_reads_approval_fields();
    void parse_decision_request_reads_structured_fields();
    void parse_unknown_json_object_falls_back_to_compact_text();
    void parse_fenced_json_extracts_structured_response();
    void parse_multiple_objects_handles_braces_inside_strings();
    void parse_plain_text_falls_back_to_raw_text();
    void streaming_preview_extracts_partial_final_summary();
    void streaming_preview_decodes_json_escapes();
    void streaming_preview_hides_non_final_json();
    void streaming_preview_hides_mixed_tool_payload_leaks();
    void streaming_preview_extracts_final_summary_from_mixed_payload();
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

void agent_messages_test_t::chat_message_round_trips_image_attachments()
{
    chat_message_t source;
    source.role = QStringLiteral("user");
    source.content = QStringLiteral("Please inspect this screenshot.");
    source.attachments = {
        image_attachment_t{QStringLiteral("img-1"), QStringLiteral("screenshot.png"),
                           QStringLiteral("/tmp/screenshot.png"), QStringLiteral("image/png")}};

    const QJsonObject json = source.to_json();
    const chat_message_t parsed = chat_message_t::from_json(json);

    QCOMPARE(parsed.attachments.size(), 1);
    QCOMPARE(parsed.attachments.constFirst().attachment_id, QStringLiteral("img-1"));
    QCOMPARE(parsed.attachments.constFirst().file_name, QStringLiteral("screenshot.png"));
    QCOMPARE(parsed.attachments.constFirst().storage_path, QStringLiteral("/tmp/screenshot.png"));
    QCOMPARE(parsed.attachments.constFirst().mime_type, QStringLiteral("image/png"));
}

void agent_messages_test_t::decision_request_round_trips_through_json_and_helpers()
{
    const agent_decision_request_t source{
        QStringLiteral("decision-helpers"),
        QStringLiteral("Choose a path"),
        QStringLiteral("Need a trade-off decision"),
        {agent_decision_option_t{QStringLiteral("fast"), QStringLiteral("Fast"),
                                 QStringLiteral("Run targeted checks only")},
         agent_decision_option_t{QStringLiteral("full"), QStringLiteral("Full"),
                                 QStringLiteral("Run full validation")}},
        true,
        QStringLiteral("Describe another strategy"),
        QStringLiteral("full")};

    const QJsonObject json = source.to_json();
    const agent_decision_request_t parsed = agent_decision_request_t::from_json(json);

    QCOMPARE(parsed.request_id, source.request_id);
    QCOMPARE(parsed.title, source.title);
    QCOMPARE(parsed.description, source.description);
    QCOMPARE(parsed.options.size(), 2);
    QCOMPARE(parsed.options.at(0).id, QStringLiteral("fast"));
    QCOMPARE(parsed.options.at(1).label, QStringLiteral("Full"));
    QVERIFY(parsed.allow_freeform);
    QCOMPARE(parsed.freeform_placeholder, QStringLiteral("Describe another strategy"));
    QCOMPARE(parsed.recommended_option_id, QStringLiteral("full"));
    QCOMPARE(parsed.option_index(QStringLiteral("fast")), 0);
    QCOMPARE(parsed.option_index(QStringLiteral("full")), 1);
    QCOMPARE(parsed.option_index(QStringLiteral("missing")), -1);
    QVERIFY(parsed.is_valid());
    QVERIFY(agent_decision_request_t{}.is_valid() == false);
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

void agent_messages_test_t::parse_decision_request_reads_structured_fields()
{
    const agent_response_t response = agent_response_t::parse(QStringLiteral(
        R"({"type":"decision_request","request_id":"decide-build","title":"Choose validation path","description":"Need your preference","options":[{"id":"fast","label":"Fast","description":"Targeted tests only"},{"id":"full","label":"Full","description":"Run the whole suite"}],"allow_freeform":true,"freeform_placeholder":"Describe another strategy","recommended_option_id":"full"})"));

    QVERIFY(response.type == response_type_t::DECISION_REQUEST);
    QCOMPARE(response.decision_request.request_id, QStringLiteral("decide-build"));
    QCOMPARE(response.decision_request.title, QStringLiteral("Choose validation path"));
    QCOMPARE(response.decision_request.description, QStringLiteral("Need your preference"));
    QCOMPARE(response.decision_request.options.size(), 2);
    QCOMPARE(response.decision_request.options.at(0).id, QStringLiteral("fast"));
    QCOMPARE(response.decision_request.options.at(0).label, QStringLiteral("Fast"));
    QCOMPARE(response.decision_request.options.at(0).description,
             QStringLiteral("Targeted tests only"));
    QCOMPARE(response.decision_request.options.at(1).id, QStringLiteral("full"));
    QCOMPARE(response.decision_request.recommended_option_id, QStringLiteral("full"));
    QVERIFY(response.decision_request.allow_freeform);
    QCOMPARE(response.decision_request.freeform_placeholder,
             QStringLiteral("Describe another strategy"));
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

void agent_messages_test_t::streaming_preview_extracts_partial_final_summary()
{
    const QString raw = QStringLiteral(R"({"type":"final","summary":"# Title\n\nHello, **wo)");

    QCOMPARE(streaming_response_markdown_preview(raw), QStringLiteral("# Title\n\nHello, **wo"));
}

void agent_messages_test_t::streaming_preview_decodes_json_escapes()
{
    const QString raw =
        QStringLiteral(R"({"type":"final","summary":"Line 1\nLine 2 with \"quotes\""})");

    QCOMPARE(streaming_response_markdown_preview(raw),
             QStringLiteral("Line 1\nLine 2 with \"quotes\""));
}

void agent_messages_test_t::streaming_preview_hides_non_final_json()
{
    const QString raw =
        QStringLiteral(R"({"type":"tool_call","name":"search_repo","args":{"query":"todo"}})");

    QVERIFY(streaming_response_markdown_preview(raw).isEmpty());

    const QString decision_raw = QStringLiteral(
        R"({"type":"decision_request","request_id":"d1","title":"Choose","options":[{"id":"a","label":"A"}],"allow_freeform":true})");
    QVERIFY(streaming_response_markdown_preview(decision_raw).isEmpty());
}

void agent_messages_test_t::streaming_preview_hides_mixed_tool_payload_leaks()
{
    const QString raw = QStringLiteral(
        "Let me investigate more deeply — read the header and other relevant files.\n</s>\n\n"
        R"({"type":"tool_call","name":"read_file","args":{"path":"src/name_table.h"}})");

    QVERIFY(streaming_response_markdown_preview(raw).isEmpty());
}

void agent_messages_test_t::streaming_preview_extracts_final_summary_from_mixed_payload()
{
    const QString raw =
        QStringLiteral("Preface that should not leak into the Actions Log.\n</s>\n\n"
                       R"({"type":"final","summary":"Fixed the bug.","diff":""})");

    QCOMPARE(streaming_response_markdown_preview(raw), QStringLiteral("Fixed the bug."));
}

QTEST_APPLESS_MAIN(agent_messages_test_t)

#include "tst_agent_messages.moc"
