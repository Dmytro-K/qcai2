/*! Unit tests for agent safety policy checks. */

#include <QtTest>

#include "src/safety/SafetyPolicy.h"

#include <QFile>
#include <QTemporaryDir>

using namespace qcai2;

class SafetyPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void isCommandAllowed_acceptsAllowlistedBasenameAndAbsolutePath();
    void requiresApproval_enforcesToolAndDiffLimits();
    void isPathSafe_acceptsWorkspaceFilesAndRejectsExternalOrMissingPaths();
};

void SafetyPolicyTest::isCommandAllowed_acceptsAllowlistedBasenameAndAbsolutePath()
{
    SafetyPolicy policy;

    QVERIFY(policy.isCommandAllowed(QStringLiteral("cmake")));
    QVERIFY(policy.isCommandAllowed(QStringLiteral("/usr/bin/cmake")));
    QVERIFY(policy.isCommandAllowed(QStringLiteral("/usr/bin/ctest")));
    QVERIFY(!policy.isCommandAllowed(QStringLiteral("python")));
}

void SafetyPolicyTest::requiresApproval_enforcesToolAndDiffLimits()
{
    SafetyPolicy policy;
    policy.setMaxChangedFiles(2);
    policy.setMaxDiffLines(5);

    QCOMPARE(policy.requiresApproval(QStringLiteral("read_file"), 1, 3), QString());
    QCOMPARE(policy.requiresApproval(QStringLiteral("apply_patch"), 1, 1),
             QStringLiteral("Tool 'apply_patch' modifies files and requires approval."));
    QCOMPARE(policy.requiresApproval(QStringLiteral("write_file"), 3, 1),
             QStringLiteral("Too many files changed (3 > 2)."));
    QCOMPARE(policy.requiresApproval(QStringLiteral("write_file"), 2, 7),
             QStringLiteral("Too many lines changed (7 > 5)."));
}

void SafetyPolicyTest::isPathSafe_acceptsWorkspaceFilesAndRejectsExternalOrMissingPaths()
{
    QTemporaryDir workspaceDir;
    QVERIFY(workspaceDir.isValid());
    QTemporaryDir externalDir;
    QVERIFY(externalDir.isValid());

    const QString workspaceFile = workspaceDir.filePath(QStringLiteral("inside.txt"));
    QFile inside(workspaceFile);
    QVERIFY(inside.open(QIODevice::WriteOnly));
    inside.write("ok");
    inside.close();

    const QString externalFile = externalDir.filePath(QStringLiteral("outside.txt"));
    QFile outside(externalFile);
    QVERIFY(outside.open(QIODevice::WriteOnly));
    outside.write("nope");
    outside.close();

    SafetyPolicy policy;

    QVERIFY(policy.isPathSafe(workspaceFile, workspaceDir.path()));
    QVERIFY(!policy.isPathSafe(externalFile, workspaceDir.path()));
    QVERIFY(!policy.isPathSafe(workspaceDir.filePath(QStringLiteral("missing.txt")), workspaceDir.path()));
}

QTEST_APPLESS_MAIN(SafetyPolicyTest)

#include "tst_safety_policy.moc"
