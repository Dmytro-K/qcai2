/*! Unit tests for agent safety policy checks. */

#include <QtTest>

#include "src/safety/SafetyPolicy.h"

#include <QFile>
#include <QTemporaryDir>

using namespace qcai2;

class safety_policy_test_t : public QObject
{
    Q_OBJECT

private slots:
    void is_command_allowed_accepts_allowlisted_basename_and_absolute_path();
    void requires_approval_enforces_tool_and_diff_limits();
    void is_path_safe_accepts_workspace_files_and_rejects_external_or_missing_paths();
};

void safety_policy_test_t::is_command_allowed_accepts_allowlisted_basename_and_absolute_path()
{
    safety_policy_t policy;

    QVERIFY(policy.is_command_allowed(QStringLiteral("cmake")));
    QVERIFY(policy.is_command_allowed(QStringLiteral("/usr/bin/cmake")));
    QVERIFY(policy.is_command_allowed(QStringLiteral("/usr/bin/ctest")));
    QVERIFY(!policy.is_command_allowed(QStringLiteral("python")));
}

void safety_policy_test_t::requires_approval_enforces_tool_and_diff_limits()
{
    safety_policy_t policy;
    policy.set_max_changed_files(2);
    policy.set_max_diff_lines(5);

    QCOMPARE(policy.requires_approval(QStringLiteral("read_file"), 1, 3), QString());
    QCOMPARE(policy.requires_approval(QStringLiteral("apply_patch"), 1, 1),
             QStringLiteral("Tool 'apply_patch' modifies files and requires approval."));
    QCOMPARE(policy.requires_approval(QStringLiteral("write_file"), 3, 1),
             QStringLiteral("Too many files changed (3 > 2)."));
    QCOMPARE(policy.requires_approval(QStringLiteral("write_file"), 2, 7),
             QStringLiteral("Too many lines changed (7 > 5)."));
}

void safety_policy_test_t::
    is_path_safe_accepts_workspace_files_and_rejects_external_or_missing_paths()
{
    QTemporaryDir workspace_dir;
    QVERIFY(workspace_dir.isValid());
    QTemporaryDir external_dir;
    QVERIFY(external_dir.isValid());

    const QString workspace_file = workspace_dir.filePath(QStringLiteral("inside.txt"));
    QFile inside(workspace_file);
    QVERIFY(inside.open(QIODevice::WriteOnly));
    inside.write("ok");
    inside.close();

    const QString external_file = external_dir.filePath(QStringLiteral("outside.txt"));
    QFile outside(external_file);
    QVERIFY(outside.open(QIODevice::WriteOnly));
    outside.write("nope");
    outside.close();

    safety_policy_t policy;

    QVERIFY(policy.is_path_safe(workspace_file, workspace_dir.path()));
    QVERIFY(!policy.is_path_safe(external_file, workspace_dir.path()));
    QVERIFY(!policy.is_path_safe(workspace_dir.filePath(QStringLiteral("missing.txt")),
                                 workspace_dir.path()));
}

QTEST_APPLESS_MAIN(safety_policy_test_t)

#include "tst_safety_policy.moc"
