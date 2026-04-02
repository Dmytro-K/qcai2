#include "../src/ui/actions_log_model.h"

#include <QtTest>

namespace qcai2
{

class tst_actions_log_model_t : public QObject
{
    Q_OBJECT

private slots:
    void append_committed_entries_preserves_order();
    void streaming_entries_lifecycle_updates_trailing_rows();
    void selected_raw_markdown_joins_rows_in_visual_order();
};

void tst_actions_log_model_t::append_committed_entries_preserves_order()
{
    actions_log_model_t model;

    model.append_committed_entry(QStringLiteral("first raw"), QStringLiteral("first rendered"));
    model.append_committed_entry(QStringLiteral("second raw"), QStringLiteral("second rendered"));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), actions_log_model_t::RAW_MARKDOWN_ROLE).toString(),
             QStringLiteral("first raw"));
    QCOMPARE(model.data(model.index(1, 0), actions_log_model_t::RENDERED_MARKDOWN_ROLE).toString(),
             QStringLiteral("second rendered"));
    QVERIFY(model.data(model.index(0, 0), actions_log_model_t::STREAMING_ROLE).toBool() == false);
}

void tst_actions_log_model_t::streaming_entries_lifecycle_updates_trailing_rows()
{
    actions_log_model_t model;
    model.append_committed_entry(QStringLiteral("committed raw"),
                                 QStringLiteral("committed rendered"));

    model.set_streaming_entries(
        {QStringLiteral("thinking raw"), QStringLiteral("draft raw")},
        {QStringLiteral("thinking rendered"), QStringLiteral("draft rendered")});
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.data(model.index(1, 0), actions_log_model_t::STREAMING_ROLE).toBool());
    QVERIFY(model.data(model.index(2, 0), actions_log_model_t::STREAMING_ROLE).toBool());
    QCOMPARE(model.data(model.index(1, 0), actions_log_model_t::RAW_MARKDOWN_ROLE).toString(),
             QStringLiteral("thinking raw"));
    QCOMPARE(model.data(model.index(2, 0), actions_log_model_t::RAW_MARKDOWN_ROLE).toString(),
             QStringLiteral("draft raw"));

    model.set_streaming_entries({QStringLiteral("thinking raw 2")},
                                {QStringLiteral("thinking rendered 2")});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1, 0), actions_log_model_t::RAW_MARKDOWN_ROLE).toString(),
             QStringLiteral("thinking raw 2"));

    model.commit_streaming_entries(
        {QStringLiteral("final raw 1"), QStringLiteral("final raw 2")},
        {QStringLiteral("final rendered 1"), QStringLiteral("final rendered 2")});
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.data(model.index(1, 0), actions_log_model_t::STREAMING_ROLE).toBool() == false);
    QCOMPARE(model.data(model.index(1, 0), actions_log_model_t::RAW_MARKDOWN_ROLE).toString(),
             QStringLiteral("final raw 1"));
    QCOMPARE(model.data(model.index(2, 0), actions_log_model_t::RAW_MARKDOWN_ROLE).toString(),
             QStringLiteral("final raw 2"));

    model.set_streaming_entries({QStringLiteral("tail raw")}, {QStringLiteral("tail rendered")});
    QCOMPARE(model.rowCount(), 4);
    model.clear_streaming_entries();
    QCOMPARE(model.rowCount(), 3);
}

void tst_actions_log_model_t::selected_raw_markdown_joins_rows_in_visual_order()
{
    actions_log_model_t model;
    model.set_committed_entries(
        {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")},
        {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")});

    const QModelIndexList indexes = {model.index(2, 0), model.index(0, 0)};
    QCOMPARE(model.selected_raw_markdown(indexes), QStringLiteral("first\n\nthird"));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_actions_log_model_t)

#include "tst_actions_log_model.moc"
