/*! @file
    @brief Unit tests for tool_call_t JSON serialization and parsing.
*/

#include <QtTest>

#include "src/models/ToolCall.h"

using namespace qcai2;

/**
 * @brief Verifies the JSON contract used to exchange tool call state.
 */
class tool_call_test_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Omits result fields until a tool call has actually executed.
     */
    void to_json_omits_optional_fields_until_executed();

    /**
     * @brief Serializes execution state, result text, and error details.
     */
    void to_json_includes_execution_result_and_error();

    /**
     * @brief Restores identifiers, tool names, and arguments from JSON input.
     */
    void from_json_reads_id_name_and_args();
};

void tool_call_test_t::to_json_omits_optional_fields_until_executed()
{
    tool_call_t call;
    call.name = "read_file";
    call.args = QJsonObject{{"path", "README.md"}};

    const QJsonObject json = call.to_json();

    QVERIFY(!json.contains("id"));
    QVERIFY(!json.contains("result"));
    QVERIFY(!json.contains("failed"));
    QCOMPARE(json.value("name").toString(), QString("read_file"));
    QCOMPARE(json.value("args").toObject().value("path").toString(), QString("README.md"));
}

void tool_call_test_t::to_json_includes_execution_result_and_error()
{
    tool_call_t call;
    call.id = "call-1";
    call.name = "run_tests";
    call.args = QJsonObject{{"filter", "json"}};
    call.executed = true;
    call.failed = true;
    call.result = "failed";
    call.error_msg = "boom";

    const QJsonObject json = call.to_json();

    QCOMPARE(json.value("id").toString(), QString("call-1"));
    QCOMPARE(json.value("result").toString(), QString("failed"));
    QVERIFY(json.value("failed").toBool());
    QCOMPARE(json.value("error").toString(), QString("boom"));
}

void tool_call_test_t::from_json_reads_id_name_and_args()
{
    const QJsonObject source{{"id", "call-2"},
                             {"name", "search_repo"},
                             {"args", QJsonObject{{"pattern", "TODO"}, {"path", "src"}}},
                             {"result", "ignored on parse"}};

    const tool_call_t call = tool_call_t::from_json(source);

    QCOMPARE(call.id, QString("call-2"));
    QCOMPARE(call.name, QString("search_repo"));
    QCOMPARE(call.args.value("pattern").toString(), QString("TODO"));
    QCOMPARE(call.args.value("path").toString(), QString("src"));
    QVERIFY(!call.executed);
}

QTEST_APPLESS_MAIN(tool_call_test_t)

#include "tst_toolcall.moc"
