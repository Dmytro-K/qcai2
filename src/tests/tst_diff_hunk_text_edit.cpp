/*! Covers conversion of unified-diff hunks into editor text replacements. */
#include "../src/diff/diff_hunk_text_edit.h"

#include <QtTest>

using namespace qcai2;

class tst_diff_hunk_text_edit_t : public QObject
{
    Q_OBJECT

private slots:
    void builds_replace_range_for_modified_hunk();
    void adjusts_positions_after_prior_accepted_hunk();
    void preserves_missing_trailing_newline();
    void rejects_stale_document_context();
};

void tst_diff_hunk_text_edit_t::builds_replace_range_for_modified_hunk()
{
    diff_hunk_t hunk;
    hunk.start_line_old = 2;
    hunk.count_old = 3;
    hunk.count_new = 3;
    hunk.hunk_body = QStringLiteral(" keep_a();\n"
                                    "-old_value();\n"
                                    "+new_value();\n"
                                    " keep_b();");

    const QString document_text =
        QStringLiteral("before();\nkeep_a();\nold_value();\nkeep_b();\nafter();\n");
    QString error_message;
    const auto edit = build_hunk_text_edit(hunk, 0, document_text, &error_message);

    QVERIFY2(edit.has_value(), qPrintable(error_message));
    QCOMPARE(edit->start_position, QStringLiteral("before();\n").size());
    QCOMPARE(edit->expected_text, QStringLiteral("keep_a();\nold_value();\nkeep_b();\n"));
    QCOMPARE(edit->replacement_text, QStringLiteral("keep_a();\nnew_value();\nkeep_b();\n"));
}

void tst_diff_hunk_text_edit_t::adjusts_positions_after_prior_accepted_hunk()
{
    diff_hunk_t hunk;
    hunk.start_line_old = 2;
    hunk.count_old = 1;
    hunk.count_new = 1;
    hunk.hunk_body = QStringLiteral("-target();\n"
                                    "+updated();");

    const QString document_text =
        QStringLiteral("inserted();\nline_one();\ntarget();\nline_three();\n");
    QString error_message;
    const auto edit = build_hunk_text_edit(hunk, 1, document_text, &error_message);

    QVERIFY2(edit.has_value(), qPrintable(error_message));
    QCOMPARE(edit->start_position, QStringLiteral("inserted();\nline_one();\n").size());
    QCOMPARE(edit->expected_text, QStringLiteral("target();\n"));
    QCOMPARE(edit->replacement_text, QStringLiteral("updated();\n"));
}

void tst_diff_hunk_text_edit_t::preserves_missing_trailing_newline()
{
    diff_hunk_t hunk;
    hunk.start_line_old = 1;
    hunk.count_old = 1;
    hunk.count_new = 1;
    hunk.hunk_body = QStringLiteral("-old_value()\n"
                                    "\\ No newline at end of file\n"
                                    "+new_value()\n"
                                    "\\ No newline at end of file");

    const QString document_text = QStringLiteral("old_value()");
    QString error_message;
    const auto edit = build_hunk_text_edit(hunk, 0, document_text, &error_message);

    QVERIFY2(edit.has_value(), qPrintable(error_message));
    QCOMPARE(edit->expected_text, QStringLiteral("old_value()"));
    QCOMPARE(edit->replacement_text, QStringLiteral("new_value()"));
}

void tst_diff_hunk_text_edit_t::rejects_stale_document_context()
{
    diff_hunk_t hunk;
    hunk.start_line_old = 1;
    hunk.count_old = 1;
    hunk.count_new = 1;
    hunk.hunk_body = QStringLiteral("-expected();\n"
                                    "+replacement();");

    QString error_message;
    const auto edit =
        build_hunk_text_edit(hunk, 0, QStringLiteral("different();\n"), &error_message);

    QVERIFY(edit.has_value() == false);
    QVERIFY(error_message.contains(QStringLiteral("Document range mismatch")));
}

QTEST_APPLESS_MAIN(tst_diff_hunk_text_edit_t)

#include "tst_diff_hunk_text_edit.moc"
