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

QString compact_json(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

class migration_test_t : public QObject
{
    Q_OBJECT

private slots:
    void parse_revision_reads_version_and_build_suffix();
    void migrate_global_settings_copies_legacy_reasoning_and_stamps_revision();
    void migrate_project_state_moves_goal_and_log_to_sibling_files();
    void migrate_project_state_renames_ignored_linked_files_key();
};

void migration_test_t::parse_revision_reads_version_and_build_suffix()
{
    const Migration::revision_t revision = Migration::parse_revision("0.0.4", "-001");

    QVERIFY(revision.valid);
    QCOMPARE(revision.major, 0);
    QCOMPARE(revision.minor, 0);
    QCOMPARE(revision.patch, 4);
    QCOMPARE(revision.build, 1);
    QCOMPARE(revision.revision_string(), QString("0.0.4-001"));
}

void migration_test_t::migrate_global_settings_copies_legacy_reasoning_and_stamps_revision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settings_path = dir.filePath("settings.ini");
    QStandardPaths::setTestModeEnabled(true);
    QDir backup_dir(Migration::global_backup_dir_path());
    qInfo() << "Using settings path" << settings_path;
    qInfo() << "Using global backup dir" << backup_dir.path();
    if (backup_dir.exists() == true)
    {
        QVERIFY(backup_dir.removeRecursively());
    }

    QSettings settings(settings_path, QSettings::IniFormat);
    settings.beginGroup("qcai2");
    settings.setValue("thinkingLevel", "high");
    settings.setValue("settingsRevision", "0.0.4-000");

    QString error;
    const bool migrated = Migration::migrate_global_settings(settings, &error);
    qInfo() << "migrate_global_settings returned" << migrated << "error" << error;
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(settings.value("reasoningEffort").toString(), QString("high"));
    QCOMPARE(settings.value("settingsRevision").toString(), Migration::current_revision_string());
    qInfo() << "Migrated settings keys" << settings.allKeys();

    backup_dir = QDir(Migration::global_backup_dir_path());
    qInfo() << "Global backup dir exists" << backup_dir.exists() << "at" << backup_dir.path();
    QVERIFY2(backup_dir.exists(), qPrintable(backup_dir.path()));
    const QStringList backups = backup_dir.entryList(QStringList() << "*.tar.xz", QDir::Files);
    qInfo() << "Global backup archives" << backups;
    QCOMPARE(backups.size(), 1);
    QVERIFY(backups.constFirst().contains("global-settings__"));
}

void migration_test_t::migrate_project_state_moves_goal_and_log_to_sibling_files()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString context_dir_path = dir.filePath("project/.qcai2");
    QVERIFY(QDir().mkpath(context_dir_path));
    const QString storage_path = QDir(context_dir_path).filePath("session.json");
    qInfo() << "Using project state path" << storage_path;
    qInfo() << "Expected goal path" << Migration::project_goal_file_path(storage_path);
    qInfo() << "Expected actions log path"
            << Migration::project_actions_log_file_path(storage_path);
    qInfo() << "Expected project backup dir" << Migration::project_backup_dir_path(storage_path);
    {
        QSaveFile file(storage_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{{"goal", "Ship it"},
                               {"logMarkdown", "# Title\n\nSome log"},
                               {"status", "Ready"},
                               {"storageRevision", "0.0.4-000"}};
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(file.commit());
    }

    QString error;
    const bool migrated = Migration::migrate_project_state(storage_path, &error);
    qInfo() << "migrate_project_state returned" << migrated << "error" << error;
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile goal_file(Migration::project_goal_file_path(storage_path));
    QVERIFY2(goal_file.open(QIODevice::ReadOnly), qPrintable(goal_file.errorString()));
    QCOMPARE(QString::fromUtf8(goal_file.readAll()), QString("Ship it"));

    QFile log_file(Migration::project_actions_log_file_path(storage_path));
    QVERIFY2(log_file.open(QIODevice::ReadOnly), qPrintable(log_file.errorString()));
    QCOMPARE(QString::fromUtf8(log_file.readAll()), QString("# Title\n\nSome log"));

    QFile state_file(storage_path);
    QVERIFY2(state_file.open(QIODevice::ReadOnly), qPrintable(state_file.errorString()));
    const QJsonDocument doc = QJsonDocument::fromJson(state_file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    qInfo() << "Migrated project state" << compact_json(root);
    QVERIFY(!root.contains("goal"));
    QVERIFY(!root.contains("logMarkdown"));
    QCOMPARE(root.value("storageRevision").toString(), Migration::current_revision_string());

    const QDir backup_dir(Migration::project_backup_dir_path(storage_path));
    qInfo() << "Project backup dir exists" << backup_dir.exists() << "at" << backup_dir.path();
    QVERIFY2(backup_dir.exists(), qPrintable(backup_dir.path()));
    const QStringList backups = backup_dir.entryList(QStringList() << "*.tar.xz", QDir::Files);
    qInfo() << "Project backup archives" << backups;
    QCOMPARE(backups.size(), 1);
    QVERIFY(backups.constFirst().contains("session__"));
}

void migration_test_t::migrate_project_state_renames_ignored_linked_files_key()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString context_dir_path = dir.filePath("project/.qcai2");
    QVERIFY(QDir().mkpath(context_dir_path));
    const QString storage_path = QDir(context_dir_path).filePath("session.json");
    {
        QSaveFile file(storage_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{
            {"excludedDefaultLinkedFiles",
             QJsonArray{QStringLiteral("src/Main.cpp"), QStringLiteral("README.md")}},
            {"storageRevision", "0.0.5-001"}};
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(file.commit());
    }

    QString error;
    const bool migrated = Migration::migrate_project_state(storage_path, &error);
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile state_file(storage_path);
    QVERIFY2(state_file.open(QIODevice::ReadOnly), qPrintable(state_file.errorString()));
    const QJsonDocument doc = QJsonDocument::fromJson(state_file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QVERIFY(!root.contains("excludedDefaultLinkedFiles"));
    QVERIFY(root.contains("ignoredLinkedFiles"));
    QCOMPARE(root.value("storageRevision").toString(), Migration::current_revision_string());

    const QJsonArray ignored_linked_files = root.value("ignoredLinkedFiles").toArray();
    QCOMPARE(ignored_linked_files.size(), 2);
    QCOMPARE(ignored_linked_files.at(0).toString(), QString("src/Main.cpp"));
    QCOMPARE(ignored_linked_files.at(1).toString(), QString("README.md"));
}

QTEST_APPLESS_MAIN(migration_test_t)

#include "tst_migration.moc"
