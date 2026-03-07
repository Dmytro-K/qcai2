#include <QtTest>

#include "src/models/ToolCall.h"

using namespace qcai2;

class ToolCallTest : public QObject
{
    Q_OBJECT

private slots:
    void toJson_omitsOptionalFieldsUntilExecuted();
    void toJson_includesExecutionResultAndError();
    void fromJson_readsIdNameAndArgs();
};

void ToolCallTest::toJson_omitsOptionalFieldsUntilExecuted()
{
    ToolCall call;
    call.name = "read_file";
    call.args = QJsonObject{{"path", "README.md"}};

    const QJsonObject json = call.toJson();

    QVERIFY(!json.contains("id"));
    QVERIFY(!json.contains("result"));
    QVERIFY(!json.contains("failed"));
    QCOMPARE(json.value("name").toString(), QString("read_file"));
    QCOMPARE(json.value("args").toObject().value("path").toString(), QString("README.md"));
}

void ToolCallTest::toJson_includesExecutionResultAndError()
{
    ToolCall call;
    call.id = "call-1";
    call.name = "run_tests";
    call.args = QJsonObject{{"filter", "json"}};
    call.executed = true;
    call.failed = true;
    call.result = "failed";
    call.errorMsg = "boom";

    const QJsonObject json = call.toJson();

    QCOMPARE(json.value("id").toString(), QString("call-1"));
    QCOMPARE(json.value("result").toString(), QString("failed"));
    QVERIFY(json.value("failed").toBool());
    QCOMPARE(json.value("error").toString(), QString("boom"));
}

void ToolCallTest::fromJson_readsIdNameAndArgs()
{
    const QJsonObject source{{"id", "call-2"},
                             {"name", "search_repo"},
                             {"args", QJsonObject{{"pattern", "TODO"}, {"path", "src"}}},
                             {"result", "ignored on parse"}};

    const ToolCall call = ToolCall::fromJson(source);

    QCOMPARE(call.id, QString("call-2"));
    QCOMPARE(call.name, QString("search_repo"));
    QCOMPARE(call.args.value("pattern").toString(), QString("TODO"));
    QCOMPARE(call.args.value("path").toString(), QString("src"));
    QVERIFY(!call.executed);
}

QTEST_APPLESS_MAIN(ToolCallTest)

#include "tst_toolcall.moc"
