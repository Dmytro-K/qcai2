#include "../src/ui/request_queue.h"

#include <QtTest>

namespace qcai2
{

class tst_request_queue_t : public QObject
{
    Q_OBJECT

private slots:
    void enqueue_and_take_preserve_fifo();
    void invalid_requests_are_rejected();
    void serialization_roundtrip_skips_invalid_entries();
    void display_text_compacts_whitespace_and_truncates();
};

void tst_request_queue_t::enqueue_and_take_preserve_fifo()
{
    request_queue_t queue;

    queued_request_t first;
    first.goal = QStringLiteral("First task");
    first.run_mode = agent_controller_t::run_mode_t::ASK;

    queued_request_t second;
    second.goal = QStringLiteral("Second task");
    second.run_mode = agent_controller_t::run_mode_t::AGENT;
    second.linked_files = {QStringLiteral("a.cpp"), QStringLiteral("b.h")};

    QVERIFY(queue.enqueue(first));
    QVERIFY(queue.enqueue(second));
    QCOMPARE(queue.size(), 2);

    queued_request_t taken;
    QVERIFY(queue.take_next(&taken));
    QCOMPARE(taken.goal, QStringLiteral("First task"));
    QCOMPARE(taken.run_mode, agent_controller_t::run_mode_t::ASK);

    QVERIFY(queue.take_next(&taken));
    QCOMPARE(taken.goal, QStringLiteral("Second task"));
    QCOMPARE(taken.linked_files.size(), 2);
    QVERIFY(queue.is_empty());
}

void tst_request_queue_t::invalid_requests_are_rejected()
{
    request_queue_t queue;
    queued_request_t invalid_request;
    invalid_request.goal = QStringLiteral("   ");

    QVERIFY(queue.enqueue(invalid_request) == false);
    QCOMPARE(queue.size(), 0);

    queued_request_t taken;
    QVERIFY(queue.take_next(&taken) == false);
}

void tst_request_queue_t::serialization_roundtrip_skips_invalid_entries()
{
    request_queue_t queue;

    queued_request_t request;
    request.goal = QStringLiteral("Investigate queue serialization");
    request.request_context = QStringLiteral("linked context");
    request.linked_files = {QStringLiteral("src/main.cpp")};
    request.attachments = {
        image_attachment_t{QStringLiteral("img-1"), QStringLiteral("diagram.png"),
                           QStringLiteral(".qcai2/conversations/c1.attachments/diagram.png"),
                           QStringLiteral("image/png")}};
    request.dry_run = false;
    request.run_mode = agent_controller_t::run_mode_t::ASK;
    request.model_name = QStringLiteral("gpt-5.4");
    request.reasoning_effort = QStringLiteral("medium");
    request.thinking_level = QStringLiteral("high");
    QVERIFY(queue.enqueue(request));

    QJsonArray serialized = queue.to_json();
    serialized.append(QJsonObject{{QStringLiteral("goal"), QStringLiteral("   ")}});

    request_queue_t restored;
    restored.restore(serialized);

    QCOMPARE(restored.size(), 1);
    queued_request_t taken;
    QVERIFY(restored.take_next(&taken));
    QCOMPARE(taken.goal, request.goal);
    QCOMPARE(taken.request_context, request.request_context);
    QCOMPARE(taken.linked_files, request.linked_files);
    QCOMPARE(taken.attachments.size(), 1);
    QCOMPARE(taken.attachments.constFirst().attachment_id, QStringLiteral("img-1"));
    QCOMPARE(taken.attachments.constFirst().file_name, QStringLiteral("diagram.png"));
    QCOMPARE(taken.dry_run, request.dry_run);
    QCOMPARE(taken.run_mode, request.run_mode);
    QCOMPARE(taken.model_name, request.model_name);
    QCOMPARE(taken.reasoning_effort, request.reasoning_effort);
    QCOMPARE(taken.thinking_level, request.thinking_level);
}

void tst_request_queue_t::display_text_compacts_whitespace_and_truncates()
{
    queued_request_t request;
    request.goal = QStringLiteral(
        "  Investigate\n\nrequest\tqueue rendering with a very long description that should be "
        "truncated for the pending items view to stay readable.  ");
    request.run_mode = agent_controller_t::run_mode_t::AGENT;
    request.linked_files = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    request.attachments = {
        image_attachment_t{QStringLiteral("img-2"), QStringLiteral("screen.png"),
                           QStringLiteral(".qcai2/conversations/c1.attachments/screen.png"),
                           QStringLiteral("image/png")}};

    const QString text = request.display_text();

    QVERIFY(text.startsWith(QStringLiteral("Agent — Investigate request queue rendering")));
    QVERIFY(text.contains(QStringLiteral("[3 linked]")));
    QVERIFY(text.contains(QStringLiteral("[1 attachments]")));
    QVERIFY(text.contains(QLatin1String("...")));
    QVERIFY(text.contains(QLatin1Char('\n')) == false);
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_request_queue_t)

#include "tst_request_queue.moc"
