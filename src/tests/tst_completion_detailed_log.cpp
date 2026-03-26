#include "../src/util/completion_detailed_log.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qcai2;

class tst_completion_detailed_log_t : public QObject
{
    Q_OBJECT

private slots:
    void writes_one_file_per_request_inside_launch_directory();
};

void tst_completion_detailed_log_t::writes_one_file_per_request_inside_launch_directory()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    completion_detailed_log_t first_log;
    first_log.begin_request(
        temp_dir.path(), QStringLiteral("completion-1"), QStringLiteral("copilot"),
        QStringLiteral("gpt-5.4"), QStringLiteral("medium"), QStringLiteral("/tmp/main.cpp"), 42,
        12, 7, 60000, QStringLiteral("int value = "), QStringLiteral(";"),
        QStringLiteral("Complete the code at <CURSOR>."),
        QList<completion_log_prompt_part_t>{
            {QStringLiteral("system"), QStringLiteral("System prompt"), 3},
            {QStringLiteral("user"), QStringLiteral("Complete the code at <CURSOR>."), 6},
        });
    first_log.record_stage(QStringLiteral("local"), QStringLiteral("processor.perform_started"),
                           QStringLiteral("file=/tmp/main.cpp"));
    first_log.record_provider_event({provider_raw_event_kind_t::REQUEST_STARTED,
                                     QStringLiteral("copilot"),
                                     QStringLiteral("request.dispatched"),
                                     {},
                                     QStringLiteral("sidecar_request_id=2")});
    first_log.record_provider_event({provider_raw_event_kind_t::IDLE,
                                     QStringLiteral("copilot"),
                                     QStringLiteral("session.idle"),
                                     {},
                                     {}});
    provider_usage_t usage;
    usage.input_tokens = 10;
    usage.output_tokens = 4;
    usage.total_tokens = 14;
    first_log.finish_request(
        QStringLiteral("error"), {},
        QStringLiteral("Completion error: Timeout after 60000ms waiting for session.idle"), usage);

    QString error;
    QVERIFY2(first_log.append_to_project_log(&error), qPrintable(error));

    completion_detailed_log_t second_log;
    second_log.begin_request(
        temp_dir.path(), QStringLiteral("completion-2"), QStringLiteral("openai"),
        QStringLiteral("gpt-5.4-mini"), QStringLiteral("off"), QStringLiteral("/tmp/other.cpp"),
        11, 5, 3, -1, QStringLiteral("auto "), QStringLiteral("{}"),
        QStringLiteral("Return only the completion."),
        QList<completion_log_prompt_part_t>{
            {QStringLiteral("system"), QStringLiteral("Return only code"), 4},
            {QStringLiteral("user"), QStringLiteral("Return only the completion."), 5},
        });
    second_log.record_stage(QStringLiteral("local"), QStringLiteral("proposal.ready"),
                            QStringLiteral("items=1 completion_chars=5"));
    second_log.finish_request(QStringLiteral("success"), QStringLiteral("value"), {}, {});
    QVERIFY2(second_log.append_to_project_log(&error), qPrintable(error));

    const QDir logs_dir(QDir(temp_dir.path()).filePath(QStringLiteral(".qcai2/logs")));
    QVERIFY(logs_dir.exists());

    const QStringList launch_dirs = logs_dir.entryList(
        QStringList{QStringLiteral("completion-log-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(launch_dirs.size(), 1);

    const QDir launch_dir(logs_dir.filePath(launch_dirs.constFirst()));
    const QStringList log_files =
        launch_dir.entryList(QStringList{QStringLiteral("completion-*.md")}, QDir::Files);
    QCOMPARE(log_files.size(), 2);
    QVERIFY(log_files.contains(QStringLiteral("completion-1.md")));
    QVERIFY(log_files.contains(QStringLiteral("completion-2.md")));

    QFile first_file(launch_dir.filePath(QStringLiteral("completion-1.md")));
    QVERIFY(first_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString first_content = QString::fromUtf8(first_file.readAll());

    QFile second_file(launch_dir.filePath(QStringLiteral("completion-2.md")));
    QVERIFY(second_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString second_content = QString::fromUtf8(second_file.readAll());

    QVERIFY(first_content.contains(QStringLiteral("# qcai2 detailed completion log")));
    QVERIFY(first_content.contains(QStringLiteral("- request_id: `completion-1`")));
    QVERIFY(first_content.contains(QStringLiteral("request.dispatched")));
    QVERIFY(first_content.contains(QStringLiteral("session.idle")));
    QVERIFY(
        first_content.contains(QStringLiteral("Timeout after 60000ms waiting for session.idle")));
    QVERIFY(first_content.contains(QStringLiteral("configured_timeout_ms: `60000`")));
    QVERIFY(first_content.contains(QStringLiteral("### Timeline")));
    QVERIFY(first_content.contains(QStringLiteral(
        "One Markdown file per completion request inside one per-launch directory")));

    QVERIFY(second_content.contains(QStringLiteral("# qcai2 detailed completion log")));
    QVERIFY(second_content.contains(QStringLiteral("- request_id: `completion-2`")));
    QVERIFY(second_content.contains(QStringLiteral("proposal.ready")));
    QVERIFY(second_content.contains(QStringLiteral("value")));
    QVERIFY(second_content.contains(QStringLiteral("- status: `success`")));

    const QStringList system_logs =
        logs_dir.entryList(QStringList{QStringLiteral("system-*.md")}, QDir::Files);
    QCOMPARE(system_logs.size(), 1);

    QFile system_file(logs_dir.filePath(system_logs.constFirst()));
    QVERIFY(system_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString system_content = QString::fromUtf8(system_file.readAll());

    QVERIFY(system_content.contains(QStringLiteral("# qcai2 system diagnostics log")));
    QVERIFY(system_content.contains(QStringLiteral("CompletionLog")));
    QVERIFY(system_content.contains(QStringLiteral("append.started")));
    QVERIFY(system_content.contains(QStringLiteral("append.succeeded")));
    QVERIFY(system_content.contains(QStringLiteral("completion-1.md")));
}

QTEST_MAIN(tst_completion_detailed_log_t)

#include "tst_completion_detailed_log.moc"
