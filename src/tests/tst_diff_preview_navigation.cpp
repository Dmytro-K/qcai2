#include "../src/ui/diff_preview_navigation.h"

#include <QtTest>

using namespace qcai2;

class tst_diff_preview_navigation_t : public QObject
{
    Q_OBJECT

private slots:
    void maps_modified_file_hunk_lines();
    void maps_new_file_to_added_path();
    void maps_deleted_file_to_original_path();
    void skips_mapping_without_project_dir();
};

void tst_diff_preview_navigation_t::maps_modified_file_hunk_lines()
{
    const QString diff = QStringLiteral("diff --git a/src/main.cpp b/src/main.cpp\n"
                                        "index 1111111..2222222 100644\n"
                                        "--- a/src/main.cpp\n"
                                        "+++ b/src/main.cpp\n"
                                        "@@ -10,2 +10,3 @@\n"
                                        " context\n"
                                        "-old_value();\n"
                                        "+new_value();\n"
                                        "+log_value();\n"
                                        " tail();\n");

    const QHash<int, diff_preview_navigation_target_t> targets =
        build_diff_preview_navigation_targets(diff, QStringLiteral("/tmp/project"));

    QVERIFY(targets.contains(1) == false);
    QVERIFY(targets.contains(5) == false);
    QVERIFY(targets.contains(6) == false);
    QCOMPARE(targets.value(7).line, 11);
    QCOMPARE(targets.value(8).line, 11);
    QCOMPARE(targets.value(9).line, 12);
    QVERIFY(targets.contains(10) == false);
}

void tst_diff_preview_navigation_t::maps_new_file_to_added_path()
{
    const QString diff = QStringLiteral("diff --git a/include/example.h b/include/example.h\n"
                                        "new file mode 100644\n"
                                        "--- /dev/null\n"
                                        "+++ b/include/example.h\n"
                                        "@@ -0,0 +1,2 @@\n"
                                        "+#pragma once\n"
                                        "+int value();\n");

    const QHash<int, diff_preview_navigation_target_t> targets =
        build_diff_preview_navigation_targets(diff, QStringLiteral("/tmp/project"));

    QCOMPARE(targets.value(6).line, 1);
    QCOMPARE(targets.value(7).line, 2);
    QVERIFY(targets.contains(4) == false);
    QVERIFY(targets.contains(5) == false);
}

void tst_diff_preview_navigation_t::maps_deleted_file_to_original_path()
{
    const QString diff = QStringLiteral("diff --git a/src/obsolete.cpp b/src/obsolete.cpp\n"
                                        "deleted file mode 100644\n"
                                        "--- a/src/obsolete.cpp\n"
                                        "+++ /dev/null\n"
                                        "@@ -3,2 +0,0 @@\n"
                                        "-legacy_call();\n"
                                        "-legacy_cleanup();\n");

    const QHash<int, diff_preview_navigation_target_t> targets =
        build_diff_preview_navigation_targets(diff, QStringLiteral("/tmp/project"));

    QCOMPARE(targets.value(6).line, 1);
    QCOMPARE(targets.value(7).line, 1);
    QVERIFY(targets.contains(3) == false);
    QVERIFY(targets.contains(5) == false);
}

void tst_diff_preview_navigation_t::skips_mapping_without_project_dir()
{
    const QHash<int, diff_preview_navigation_target_t> targets =
        build_diff_preview_navigation_targets(QStringLiteral("@@ -1 +1 @@\n-old\n+new\n"),
                                              QString());

    QVERIFY(targets.isEmpty());
}

QTEST_APPLESS_MAIN(tst_diff_preview_navigation_t)

#include "tst_diff_preview_navigation.moc"
