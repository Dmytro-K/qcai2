#include "../src/diff/diff_hunk_anchor.h"

#include <QtTest>

using namespace qcai2;

class tst_diff_hunk_anchor_t : public QObject
{
    Q_OBJECT

private slots:
    void anchors_insertion_after_leading_context();
    void anchors_deletion_after_leading_context();
    void anchors_new_file_insertions_to_first_line();
    void falls_back_to_hunk_start_when_body_has_no_changes();
};

void tst_diff_hunk_anchor_t::anchors_insertion_after_leading_context()
{
    const QString hunk_body =
        QStringLiteral(" class settings_widget_t : public Core::IOptionsPageWidget\n"
                       "         bool dry_run_default = false;\n"
                       "+        bool linked_files_include_current_file_by_default = false;\n"
                       "         bool ai_completion_enabled = false;\n");

    QCOMPARE(first_change_old_line_for_hunk(179, 179, hunk_body), 181);
}

void tst_diff_hunk_anchor_t::anchors_deletion_after_leading_context()
{
    const QString hunk_body = QStringLiteral(" void run();\n"
                                             " void stop();\n"
                                             "-void legacy_call();\n"
                                             " void flush();\n");

    QCOMPARE(first_change_old_line_for_hunk(20, 20, hunk_body), 22);
}

void tst_diff_hunk_anchor_t::anchors_new_file_insertions_to_first_line()
{
    const QString hunk_body = QStringLiteral("+#pragma once\n"
                                             "+int value();\n");

    QCOMPARE(first_change_old_line_for_hunk(0, 1, hunk_body), 1);
}

void tst_diff_hunk_anchor_t::falls_back_to_hunk_start_when_body_has_no_changes()
{
    const QString hunk_body = QStringLiteral(" unchanged();\n"
                                             " still_unchanged();\n");

    QCOMPARE(first_change_old_line_for_hunk(10, 10, hunk_body), 10);
}

QTEST_APPLESS_MAIN(tst_diff_hunk_anchor_t)

#include "tst_diff_hunk_anchor.moc"
