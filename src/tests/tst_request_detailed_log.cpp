#include "../src/util/request_detailed_log.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qcai2;

class tst_request_detailed_log_t : public QObject
{
    Q_OBJECT

private slots:
    void appends_requests_with_visible_separator();
};

void tst_request_detailed_log_t::appends_requests_with_visible_separator()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    request_detailed_log_t first_log;
    first_log.begin_request(
        temp_dir.path(), QStringLiteral("run-1"), QStringLiteral("Fix bug"),
        QStringLiteral("Linked files for this request:\n"
                       "- a.cpp\n\n"
                       "Use these linked files as explicit request context.\n\n"
                       "File: a.cpp\n"
                       "~~~~text\n"
                       "super secret linked file body\n"
                       "~~~~"),
        QStringList{QStringLiteral("a.cpp")}, QStringLiteral("openai"), QStringLiteral("gpt-5.2"),
        QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("agent"), false,
        QStringList{QStringLiteral("🔌 MCP: server `demo` ready with 1 tool(s).")});

    first_log.record_iteration_input(
        1, QStringLiteral("openai"), QStringLiteral("gpt-5.2"), QStringLiteral("medium"),
        QStringLiteral("high"), 0.2, 4096,
        QList<request_log_input_part_t>{
            {QStringLiteral("message[0] system"), QStringLiteral("System prompt"), 3},
            {QStringLiteral("message[1] system"),
             QStringLiteral("Linked files for this request:\n"
                            "- a.cpp\n\n"
                            "Use these linked files as explicit request context.\n\n"
                            "File: a.cpp\n"
                            "~~~~text\n"
                            "super secret linked file body\n"
                            "~~~~"),
             25},
            {QStringLiteral("message[2] user"), QStringLiteral("Fix bug"), 2},
        });

    provider_usage_t usage;
    usage.input_tokens = 10;
    usage.output_tokens = 5;
    usage.total_tokens = 15;
    first_log.record_iteration_output(1, QStringLiteral("{\"type\":\"final\"}"), {},
                                      QStringLiteral("final"), QStringLiteral("Done"), usage,
                                      1234);
    first_log.record_tool_event({1, QStringLiteral("mcp_demo_tool"), QJsonObject{},
                                 QStringLiteral("ok"), false, false, false, true});
    first_log.finish_request(QStringLiteral("completed"),
                             QJsonObject{{QStringLiteral("summary"), QStringLiteral("Done")}},
                             usage, QStringLiteral("diff --git a/a.cpp b/a.cpp"));

    QString error;
    QVERIFY2(first_log.append_to_project_log(&error), qPrintable(error));

    request_detailed_log_t second_log;
    second_log.begin_request(temp_dir.path(), QStringLiteral("run-2"),
                             QStringLiteral("Ask something"), {}, {}, QStringLiteral("openai"),
                             QStringLiteral("gpt-5.2"), QStringLiteral("off"),
                             QStringLiteral("off"), QStringLiteral("ask"), true, {});
    second_log.record_iteration_input(
        1, QStringLiteral("openai"), QStringLiteral("gpt-5.2"), QStringLiteral("off"),
        QStringLiteral("off"), 0.2, 4096,
        QList<request_log_input_part_t>{
            {QStringLiteral("message[0] user"), QStringLiteral("Ask something"), 4},
        });
    second_log.record_iteration_output(1, QStringLiteral("Plain text answer"), {},
                                       QStringLiteral("text"), QStringLiteral("Plain text answer"),
                                       provider_usage_t{}, 100);
    second_log.finish_request(QStringLiteral("completed"), QJsonObject{}, provider_usage_t{}, {});
    QVERIFY2(second_log.append_to_project_log(&error), qPrintable(error));

    const QDir logs_dir(QDir(temp_dir.path()).filePath(QStringLiteral(".qcai2/logs")));
    QVERIFY(logs_dir.exists());

    const QStringList log_files =
        logs_dir.entryList(QStringList{QStringLiteral("request-log-*.md")}, QDir::Files);
    QCOMPARE(log_files.size(), 1);

    QFile file(logs_dir.filePath(log_files.constFirst()));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());

    QVERIFY(content.contains(QStringLiteral("# qcai2 detailed request log")));
    QCOMPARE(content.count(QStringLiteral("\n\n---\n\n## Request `")), 2);
    QVERIFY(content.contains(QStringLiteral("### Linked Files")));
    QVERIFY(content.contains(QStringLiteral("mcp_demo_tool")));
    QVERIFY(content.contains(QStringLiteral("Plain text answer")));
    QVERIFY(content.contains(QStringLiteral("diff --git a/a.cpp b/a.cpp")));
    QVERIFY(
        content.contains(QStringLiteral("redacted because linked file contents are not logged")));
    QVERIFY(content.contains(QStringLiteral("_redacted: linked file contents are not logged._")));
    QVERIFY(content.contains(QStringLiteral("`a.cpp`")));
    QVERIFY(!content.contains(QStringLiteral("super secret linked file body")));
}

QTEST_MAIN(tst_request_detailed_log_t)

#include "tst_request_detailed_log.moc"
