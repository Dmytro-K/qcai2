/*! @file
    @brief Unit tests for revision-aware settings and project-state migrations.
*/

#include <QtTest>

#include "src/util/migration.h"

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
    void migrate_project_state_moves_conversation_ui_to_per_conversation_file();
    void migrate_project_state_repairs_partial_conversation_state_when_revision_current();
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

    QFile state_file(storage_path);
    QVERIFY2(state_file.open(QIODevice::ReadOnly), qPrintable(state_file.errorString()));
    const QJsonDocument doc = QJsonDocument::fromJson(state_file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    qInfo() << "Migrated project state" << compact_json(root);
    QVERIFY(!root.contains("goal"));
    QVERIFY(!root.contains("logMarkdown"));
    const QString conversation_id = root.value("conversationId").toString();
    QVERIFY(conversation_id.isEmpty() == false);
    QCOMPARE(root.value("storageRevision").toString(), Migration::current_revision_string());
    QVERIFY(QFileInfo::exists(Migration::project_goal_file_path(storage_path)) == false);
    QVERIFY(QFileInfo::exists(Migration::project_actions_log_file_path(storage_path)) == false);

    QFile conversation_file(
        Migration::conversation_state_file_path(storage_path, conversation_id));
    QVERIFY2(conversation_file.open(QIODevice::ReadOnly),
             qPrintable(conversation_file.errorString()));
    const QJsonDocument conversation_doc = QJsonDocument::fromJson(conversation_file.readAll());
    QVERIFY(conversation_doc.isObject());
    const QJsonObject conversation = conversation_doc.object();
    QCOMPARE(conversation.value("goal").toString(), QString("Ship it"));
    QVERIFY(!conversation.contains("logMarkdown"));

    QFile journal_file(
        Migration::conversation_log_journal_file_path(storage_path, conversation_id));
    QVERIFY2(journal_file.open(QIODevice::ReadOnly), qPrintable(journal_file.errorString()));
    const QJsonDocument journal_doc = QJsonDocument::fromJson(journal_file.readLine().trimmed());
    QVERIFY(journal_doc.isObject());
    QCOMPARE(journal_doc.object().value("markdown").toString(), QString("# Title\n\nSome log"));

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
    const QString conversation_id = root.value("conversationId").toString();
    QVERIFY(conversation_id.isEmpty() == false);
    QCOMPARE(root.value("storageRevision").toString(), Migration::current_revision_string());

    QFile conversation_file(
        Migration::conversation_state_file_path(storage_path, conversation_id));
    QVERIFY2(conversation_file.open(QIODevice::ReadOnly),
             qPrintable(conversation_file.errorString()));
    const QJsonDocument conversation_doc = QJsonDocument::fromJson(conversation_file.readAll());
    QVERIFY(conversation_doc.isObject());
    const QJsonArray ignored_linked_files =
        conversation_doc.object().value("ignored_linked_files").toArray();
    QCOMPARE(ignored_linked_files.size(), 2);
    QCOMPARE(ignored_linked_files.at(0).toString(), QString("src/Main.cpp"));
    QCOMPARE(ignored_linked_files.at(1).toString(), QString("README.md"));
}

void migration_test_t::migrate_project_state_moves_conversation_ui_to_per_conversation_file()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString context_dir_path = dir.filePath("project/.qcai2");
    QVERIFY(QDir().mkpath(context_dir_path));
    const QString storage_path = QDir(context_dir_path).filePath("session.json");
    const QString conversation_id = QStringLiteral("conversation-123");

    {
        QSaveFile file(storage_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{
            {"conversationId", conversation_id},
            {"diff", "@@ -1 +1 @@\n-old\n+new\n"},
            {"status", "Ready"},
            {"activeTab", 2},
            {"mode", "agent"},
            {"model", "gpt-5.4"},
            {"reasoningEffort", "medium"},
            {"thinkingLevel", "high"},
            {"dryRun", true},
            {"linkedFiles", QJsonArray{QStringLiteral("README.md")}},
            {"ignored_linked_files", QJsonArray{QStringLiteral("docs/CODING_STYLE.md")}},
            {"plan", QJsonArray{QStringLiteral("1. Do the thing")}},
            {"storageRevision", "0.0.5-003"},
        };
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(file.commit());
    }

    {
        QSaveFile goal_file(Migration::project_goal_file_path(storage_path));
        QVERIFY(goal_file.open(QIODevice::WriteOnly));
        goal_file.write("Ship conversation UI");
        QVERIFY(goal_file.commit());
    }

    {
        QSaveFile log_file(Migration::project_actions_log_file_path(storage_path));
        QVERIFY(log_file.open(QIODevice::WriteOnly));
        log_file.write("# Actions Log");
        QVERIFY(log_file.commit());
    }

    QString error;
    const bool migrated = Migration::migrate_project_state(storage_path, &error);
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile state_file(storage_path);
    QVERIFY2(state_file.open(QIODevice::ReadOnly), qPrintable(state_file.errorString()));
    const QJsonDocument state_doc = QJsonDocument::fromJson(state_file.readAll());
    QVERIFY(state_doc.isObject());
    const QJsonObject root = state_doc.object();
    QCOMPARE(root.value("conversationId").toString(), conversation_id);
    QVERIFY(!root.contains("model"));
    QVERIFY(!root.contains("diff"));
    QVERIFY(!root.contains("plan"));
    QCOMPARE(root.value("storageRevision").toString(), Migration::current_revision_string());

    const QString conversation_path =
        Migration::conversation_state_file_path(storage_path, conversation_id);
    QFile conversation_file(conversation_path);
    QVERIFY2(conversation_file.open(QIODevice::ReadOnly),
             qPrintable(conversation_file.errorString()));
    const QJsonDocument conversation_doc = QJsonDocument::fromJson(conversation_file.readAll());
    QVERIFY(conversation_doc.isObject());
    const QJsonObject conversation = conversation_doc.object();
    QCOMPARE(conversation.value("conversationId").toString(), conversation_id);
    QCOMPARE(conversation.value("model").toString(), QString("gpt-5.4"));
    QCOMPARE(conversation.value("goal").toString(), QString("Ship conversation UI"));
    QVERIFY(!conversation.contains("logMarkdown"));
    QCOMPARE(conversation.value("activeTab").toInt(), 2);
    QCOMPARE(conversation.value("linkedFiles").toArray().size(), 1);
    QCOMPARE(conversation.value("ignored_linked_files").toArray().size(), 1);

    QFile journal_file(
        Migration::conversation_log_journal_file_path(storage_path, conversation_id));
    QVERIFY2(journal_file.open(QIODevice::ReadOnly), qPrintable(journal_file.errorString()));
    const QJsonDocument journal_doc = QJsonDocument::fromJson(journal_file.readLine().trimmed());
    QVERIFY(journal_doc.isObject());
    QCOMPARE(journal_doc.object().value("markdown").toString(), QString("# Actions Log"));

    QVERIFY(QFileInfo::exists(Migration::project_goal_file_path(storage_path)) == false);
    QVERIFY(QFileInfo::exists(Migration::project_actions_log_file_path(storage_path)) == false);
}

void migration_test_t::
    migrate_project_state_repairs_partial_conversation_state_when_revision_current()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString context_dir_path = dir.filePath("project/.qcai2");
    QVERIFY(QDir().mkpath(context_dir_path));
    const QString storage_path = QDir(context_dir_path).filePath("session.json");
    const QString conversation_id = QStringLiteral("conversation-repair-123");

    {
        QSaveFile file(storage_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QJsonObject root{{"conversationId", conversation_id},
                               {"storageRevision", Migration::current_revision_string()}};
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(file.commit());
    }

    {
        QSaveFile goal_file(Migration::project_goal_file_path(storage_path));
        QVERIFY(goal_file.open(QIODevice::WriteOnly));
        goal_file.write("Recovered goal");
        QVERIFY(goal_file.commit());
    }

    {
        QSaveFile log_file(Migration::project_actions_log_file_path(storage_path));
        QVERIFY(log_file.open(QIODevice::WriteOnly));
        log_file.write("# Recovered log");
        QVERIFY(log_file.commit());
    }

    {
        QVERIFY(QDir().mkpath(
            QFileInfo(Migration::conversation_state_file_path(storage_path, conversation_id))
                .absolutePath()));
        QSaveFile conversation_file(
            Migration::conversation_state_file_path(storage_path, conversation_id));
        QVERIFY(conversation_file.open(QIODevice::WriteOnly));
        const QJsonObject root{{"conversationId", conversation_id},
                               {"goal", ""},
                               {"logMarkdown", "# Old embedded log"},
                               {"status", "Ready"}};
        conversation_file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QVERIFY(conversation_file.commit());
    }

    QString error;
    const bool migrated = Migration::migrate_project_state(storage_path, &error);
    QVERIFY2(migrated, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile conversation_file(
        Migration::conversation_state_file_path(storage_path, conversation_id));
    QVERIFY2(conversation_file.open(QIODevice::ReadOnly),
             qPrintable(conversation_file.errorString()));
    const QJsonDocument conversation_doc = QJsonDocument::fromJson(conversation_file.readAll());
    QVERIFY(conversation_doc.isObject());
    const QJsonObject conversation = conversation_doc.object();
    QCOMPARE(conversation.value("goal").toString(), QString("Recovered goal"));
    QVERIFY(!conversation.contains("logMarkdown"));

    QFile journal_file(
        Migration::conversation_log_journal_file_path(storage_path, conversation_id));
    QVERIFY2(journal_file.open(QIODevice::ReadOnly), qPrintable(journal_file.errorString()));
    const QJsonDocument journal_doc = QJsonDocument::fromJson(journal_file.readLine().trimmed());
    QVERIFY(journal_doc.isObject());
    QCOMPARE(journal_doc.object().value("markdown").toString(), QString("# Recovered log"));

    QVERIFY(QFileInfo::exists(Migration::project_goal_file_path(storage_path)) == false);
    QVERIFY(QFileInfo::exists(Migration::project_actions_log_file_path(storage_path)) == false);
}

QTEST_APPLESS_MAIN(migration_test_t)

#include "tst_migration.moc"
