/*! @file
    @brief Unit tests for revision-aware settings and project-state migrations.
*/

#include <QtTest>

#include "src/util/Migration.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace qcai2;

namespace
{

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

class MigrationTest : public QObject
{
    Q_OBJECT

private slots:
    void parseRevision_readsVersionAndBuildSuffix();
    void migrateGlobalSettings_copiesLegacyReasoningAndStampsRevision();
    void migrateProjectState_movesGoalAndLogToSiblingFiles();
    void migrateProjectState_renamesIgnoredLinkedFilesKey();
};

void MigrationTest::parseRevision_readsVersionAndBuildSuffix()
{
    const Migration::Revision revision = Migration::parseRevision("0.0.4", "-001");

    QVERIFY(revision.valid);
    QCOMPARE(revision.major, 0);
    QCOMPARE(revision.minor, 0);
    QCOMPARE(revision.patch, 4);
    QCOMPARE(revision.build, 1);
    QCOMPARE(revision.revisionString(), QString("0.0.4-001"));
}

void MigrationTest::migrateGlobalSettings_copiesLegacyReasoningAndStampsRevision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath("settings.ini");
    QStandardPaths::setTestModeEnabled(true);
    QDir backupDir(Migration::globalBackupDirPath());
    qInfo() << "Using settings path" << settingsPath;
    qInfo() << "Using global backup dir" << backupDir.path();
    if (backupDir.exists() == true)
    {
        QVERIFY(backupDir.removeRecursively());
    }

    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup("qcai2");
    settings.setValue("thinkingLevel", "high");
    settings.setValue("settingsRevision", "0.0.4-000");

    QString error;
    const bool migrated = Migration::migrateGlobalSettings(settings, &error);
    qInfo() << "migrateGlobalSettings returned" << migrated << "error" << error;
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(settings.value("reasoningEffort").toString(), QString("high"));
    QCOMPARE(settings.value("settingsRevision").toString(), Migration::currentRevisionString());
    qInfo() << "Migrated settings keys" << settings.allKeys();

    backupDir = QDir(Migration::globalBackupDirPath());
    qInfo() << "Global backup dir exists" << backupDir.exists() << "at" << backupDir.path();
    QVERIFY2(backupDir.exists(), qPrintable(backupDir.path()));
    const QStringList backups = backupDir.entryList(QStringList() << "*.tar.xz", QDir::Files);
    qInfo() << "Global backup archives" << backups;
    QCOMPARE(backups.size(), 1);
    QVERIFY(backups.constFirst().contains("global-settings__"));
}

void MigrationTest::migrateProjectState_movesGoalAndLogToSiblingFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString contextDirPath = dir.filePath("project/.qcai2");
    QVERIFY(QDir().mkpath(contextDirPath));
    const QString storagePath = QDir(contextDirPath).filePath("session.json");
    qInfo() << "Using project state path" << storagePath;
    qInfo() << "Expected goal path" << Migration::projectGoalFilePath(storagePath);
    qInfo() << "Expected actions log path" << Migration::projectActionsLogFilePath(storagePath);
    qInfo() << "Expected project backup dir" << Migration::projectBackupDirPath(storagePath);
    {
        QSaveFile file(storagePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{{"goal", "Ship it"},
                               {"logMarkdown", "# Title\n\nSome log"},
                               {"status", "Ready"},
                               {"storageRevision", "0.0.4-000"}};
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(file.commit());
    }

    QString error;
    const bool migrated = Migration::migrateProjectState(storagePath, &error);
    qInfo() << "migrateProjectState returned" << migrated << "error" << error;
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile goalFile(Migration::projectGoalFilePath(storagePath));
    QVERIFY2(goalFile.open(QIODevice::ReadOnly), qPrintable(goalFile.errorString()));
    QCOMPARE(QString::fromUtf8(goalFile.readAll()), QString("Ship it"));

    QFile logFile(Migration::projectActionsLogFilePath(storagePath));
    QVERIFY2(logFile.open(QIODevice::ReadOnly), qPrintable(logFile.errorString()));
    QCOMPARE(QString::fromUtf8(logFile.readAll()), QString("# Title\n\nSome log"));

    QFile stateFile(storagePath);
    QVERIFY2(stateFile.open(QIODevice::ReadOnly), qPrintable(stateFile.errorString()));
    const QJsonDocument doc = QJsonDocument::fromJson(stateFile.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    qInfo() << "Migrated project state" << compactJson(root);
    QVERIFY(!root.contains("goal"));
    QVERIFY(!root.contains("logMarkdown"));
    QCOMPARE(root.value("storageRevision").toString(), Migration::currentRevisionString());

    const QDir backupDir(Migration::projectBackupDirPath(storagePath));
    qInfo() << "Project backup dir exists" << backupDir.exists() << "at" << backupDir.path();
    QVERIFY2(backupDir.exists(), qPrintable(backupDir.path()));
    const QStringList backups = backupDir.entryList(QStringList() << "*.tar.xz", QDir::Files);
    qInfo() << "Project backup archives" << backups;
    QCOMPARE(backups.size(), 1);
    QVERIFY(backups.constFirst().contains("session__"));
}

void MigrationTest::migrateProjectState_renamesIgnoredLinkedFilesKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString contextDirPath = dir.filePath("project/.qcai2");
    QVERIFY(QDir().mkpath(contextDirPath));
    const QString storagePath = QDir(contextDirPath).filePath("session.json");
    {
        QSaveFile file(storagePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{
            {"excludedDefaultLinkedFiles",
             QJsonArray{QStringLiteral("src/Main.cpp"), QStringLiteral("README.md")}},
            {"storageRevision", "0.0.5-001"}};
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(file.commit());
    }

    QString error;
    const bool migrated = Migration::migrateProjectState(storagePath, &error);
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile stateFile(storagePath);
    QVERIFY2(stateFile.open(QIODevice::ReadOnly), qPrintable(stateFile.errorString()));
    const QJsonDocument doc = QJsonDocument::fromJson(stateFile.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QVERIFY(!root.contains("excludedDefaultLinkedFiles"));
    QVERIFY(root.contains("ignoredLinkedFiles"));
    QCOMPARE(root.value("storageRevision").toString(), Migration::currentRevisionString());

    const QJsonArray ignoredLinkedFiles = root.value("ignoredLinkedFiles").toArray();
    QCOMPARE(ignoredLinkedFiles.size(), 2);
    QCOMPARE(ignoredLinkedFiles.at(0).toString(), QString("src/Main.cpp"));
    QCOMPARE(ignoredLinkedFiles.at(1).toString(), QString("README.md"));
}

QTEST_APPLESS_MAIN(MigrationTest)

#include "tst_migration.moc"
