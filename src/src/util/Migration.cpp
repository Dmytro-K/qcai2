/*! Implements revision-aware migrations for plugin settings and project state. */

#include "Migration.h"
#include "migrations/Migration_0_0_4_001.h"

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace qcai2::Migration
{

namespace
{

#ifndef QCAI2_CURRENT_VERSION
#error "QCAI2_CURRENT_VERSION must be provided by CMake."
#endif

#ifndef QCAI2_CURRENT_BUILD_SUFFIX
#error "QCAI2_CURRENT_BUILD_SUFFIX must be provided by CMake."
#endif

constexpr auto kGlobalRevisionKey = "settingsRevision";

constexpr auto kProjectRevisionKey = "storageRevision";

constexpr auto kCurrentVersion = QCAI2_CURRENT_VERSION;
constexpr auto kCurrentBuildSuffix = QCAI2_CURRENT_BUILD_SUFFIX;

struct ArchiveEntryData
{
    QString path;
    QByteArray data;
};

struct ArchiveWriteContext
{
    QFile file;
};

Revision currentRevisionImpl()
{
    return parseRevision(QString::fromLatin1(kCurrentVersion), QString::fromLatin1(kCurrentBuildSuffix));
}

int compare(const Revision &lhs, const Revision &rhs)
{
    if (lhs.major != rhs.major)
        return lhs.major < rhs.major ? -1 : 1;
    if (lhs.minor != rhs.minor)
        return lhs.minor < rhs.minor ? -1 : 1;
    if (lhs.patch != rhs.patch)
        return lhs.patch < rhs.patch ? -1 : 1;
    if (lhs.build != rhs.build)
        return lhs.build < rhs.build ? -1 : 1;
    return 0;
}

QString readTextFile(const QString &path, bool *exists = nullptr, QString *error = nullptr)
{
    QFile file(path);
    if (!file.exists())
    {
        if (exists != nullptr)
            *exists = false;
        return {};
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (exists != nullptr)
            *exists = true;
        if (error != nullptr)
            *error = QStringLiteral("Failed to open %1 for reading").arg(path);
        return {};
    }

    if (exists != nullptr)
        *exists = true;
    return QString::fromUtf8(file.readAll());
}

QString siblingFilePath(const QString &storagePath, const QString &suffix)
{
    const QFileInfo storageInfo(storagePath);
    return storageInfo.dir().filePath(storageInfo.completeBaseName() + suffix);
}

QString revisionLabel(const Revision &revision)
{
    return revision.valid ? revision.revisionString() : QStringLiteral("unversioned");
}

QString iso8601BasicTimestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

bool checkArchiveResult(struct archive *archiveHandle, int result, QString *error)
{
    if (result == ARCHIVE_OK)
        return true;

    if (error != nullptr)
    {
        *error = QStringLiteral("libarchive error: %1")
                     .arg(QString::fromUtf8(archive_error_string(archiveHandle)));
    }
    return false;
}

void setArchiveQtError(struct archive *archiveHandle, const QString &message)
{
    const QByteArray utf8Message = message.toUtf8();
    archive_set_error(archiveHandle, EIO, "%s", utf8Message.constData());
}

int archiveOpenCallback(struct archive *archiveHandle, void *clientData)
{
    auto *context = static_cast<ArchiveWriteContext *>(clientData);
    if (context->file.isOpen())
        return ARCHIVE_OK;

    if (!context->file.open(QIODevice::WriteOnly))
    {
        setArchiveQtError(archiveHandle,
                          QStringLiteral("Failed to open %1 for writing: %2")
                              .arg(context->file.fileName(), context->file.errorString()));
        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

la_ssize_t archiveWriteCallback(struct archive *archiveHandle, void *clientData,
                                const void *buffer, size_t length)
{
    auto *context = static_cast<ArchiveWriteContext *>(clientData);
    const qint64 written =
        context->file.write(static_cast<const char *>(buffer), static_cast<qint64>(length));
    if (written < 0 || static_cast<size_t>(written) != length)
    {
        setArchiveQtError(archiveHandle,
                          QStringLiteral("Failed to write %1: %2")
                              .arg(context->file.fileName(), context->file.errorString()));
        return -1;
    }

    return static_cast<la_ssize_t>(written);
}

int archiveCloseCallback(struct archive *archiveHandle, void *clientData)
{
    auto *context = static_cast<ArchiveWriteContext *>(clientData);
    if (!context->file.isOpen())
        return ARCHIVE_OK;

    if (!context->file.flush())
    {
        setArchiveQtError(archiveHandle,
                          QStringLiteral("Failed to flush %1: %2")
                              .arg(context->file.fileName(), context->file.errorString()));
        context->file.close();
        return ARCHIVE_FATAL;
    }

    context->file.close();
    return ARCHIVE_OK;
}

bool createTarXzArchive(const QString &archivePath, const QList<ArchiveEntryData> &entries,
                        QString *error)
{
    struct archive *archiveHandle = archive_write_new();
    if (archiveHandle == nullptr)
    {
        if (error != nullptr)
            *error = QStringLiteral("Failed to allocate libarchive writer");
        return false;
    }

    ArchiveWriteContext context{QFile(archivePath)};
    bool ok = checkArchiveResult(archiveHandle, archive_write_add_filter_xz(archiveHandle), error) &&
              checkArchiveResult(archiveHandle,
                                 archive_write_set_format_pax_restricted(archiveHandle), error) &&
              checkArchiveResult(archiveHandle,
                                 archive_write_open(archiveHandle, &context, archiveOpenCallback,
                                                    archiveWriteCallback, archiveCloseCallback),
                                 error);

    for (const ArchiveEntryData &entryData : entries)
    {
        if (!ok)
            break;

        struct archive_entry *entry = archive_entry_new();
        const QByteArray encodedEntryPath = QFile::encodeName(entryData.path);
        archive_entry_copy_pathname(entry, encodedEntryPath.constData());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, entryData.data.size());
        archive_entry_set_mtime(entry, QDateTime::currentSecsSinceEpoch(), 0);

        ok = checkArchiveResult(archiveHandle, archive_write_header(archiveHandle, entry), error);
        if (ok && !entryData.data.isEmpty())
        {
            const size_t dataSize = static_cast<size_t>(entryData.data.size());
            const la_ssize_t written =
                archive_write_data(archiveHandle, entryData.data.constData(), dataSize);
            if (written < 0 || static_cast<size_t>(written) != dataSize)
            {
                if (error != nullptr)
                {
                    *error = QStringLiteral("Failed to write archive entry %1: %2")
                                 .arg(entryData.path,
                                      QString::fromUtf8(archive_error_string(archiveHandle)));
                }
                ok = false;
            }
        }

        archive_entry_free(entry);
    }

    const bool closeOk =
        ok && checkArchiveResult(archiveHandle, archive_write_close(archiveHandle), error);
    const int freeResult = archive_write_free(archiveHandle);
    bool freeOk = freeResult == ARCHIVE_OK;
    if (!freeOk && (error != nullptr))
        *error = QStringLiteral("Failed to finalize archive %1").arg(archivePath);
    return ok && closeOk && freeOk;
}

QString ensureGlobalBackupDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("qcai2_buk"));
}

QString ensureProjectBackupDir(const QString &storagePath)
{
    QDir contextDir = QFileInfo(storagePath).dir();
    if (contextDir.cdUp())
        return contextDir.filePath(QStringLiteral(".qcai2_buk"));
    return contextDir.filePath(QStringLiteral(".qcai2_buk"));
}

bool ensureDirExists(const QString &dirPath, QString *error)
{
    if (QDir().mkpath(dirPath))
        return true;
    if (error != nullptr)
        *error = QStringLiteral("Failed to create backup directory %1").arg(dirPath);
    return false;
}

QByteArray manifestJson(const QString &kind, const QString &createdAt, const Revision &fromRevision,
                        const Revision &toRevision, const QString &sourceId)
{
    const QJsonObject manifest{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("createdAt"), createdAt},
        {QStringLiteral("fromRevision"), revisionLabel(fromRevision)},
        {QStringLiteral("toRevision"), revisionLabel(toRevision)},
        {QStringLiteral("source"), sourceId},
    };
    return QJsonDocument(manifest).toJson(QJsonDocument::Indented);
}

QJsonObject settingsSnapshot(QSettings &settings)
{
    QJsonObject snapshot;
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys)
        snapshot.insert(key, QJsonValue::fromVariant(settings.value(key)));
    return snapshot;
}

bool createGlobalSettingsBackup(QSettings &settings, const Revision &fromRevision,
                                const Revision &toRevision, QString *error)
{
    const QString backupDir = ensureGlobalBackupDir();
    if (!ensureDirExists(backupDir, error))
        return false;

    const QString timestamp = iso8601BasicTimestampUtc();
    const QString archivePath = QDir(backupDir).filePath(
        QStringLiteral("global-settings__%1__%2_to_%3.tar.xz")
            .arg(timestamp, revisionLabel(fromRevision), revisionLabel(toRevision)));

    QList<ArchiveEntryData> entries;
    entries.append({QStringLiteral("settings.json"),
                    QJsonDocument(settingsSnapshot(settings)).toJson(QJsonDocument::Indented)});
    entries.append({QStringLiteral("manifest.json"),
                    manifestJson(QStringLiteral("global-settings"), timestamp, fromRevision,
                                 toRevision, QStringLiteral("QSettings/qcai2"))});
    return createTarXzArchive(archivePath, entries, error);
}

bool appendFileIfExists(QList<ArchiveEntryData> &entries, const QString &archiveName,
                        const QString &diskPath, QString *error)
{
    bool exists = false;
    QString readError;
    const QString text = readTextFile(diskPath, &exists, &readError);
    if (!readError.isEmpty())
    {
        if (error != nullptr)
            *error = readError;
        return false;
    }
    if (!exists)
        return true;
    entries.append({archiveName, text.toUtf8()});
    return true;
}

bool createProjectStateBackup(const QString &storagePath, const QJsonObject &root,
                              const Revision &fromRevision, const Revision &toRevision,
                              QString *error)
{
    const QString backupDir = ensureProjectBackupDir(storagePath);
    if (!ensureDirExists(backupDir, error))
        return false;

    const QFileInfo storageInfo(storagePath);
    const QString timestamp = iso8601BasicTimestampUtc();
    const QString archivePath = QDir(backupDir).filePath(
        QStringLiteral("%1__%2__%3_to_%4.tar.xz")
            .arg(storageInfo.completeBaseName(), timestamp, revisionLabel(fromRevision),
                 revisionLabel(toRevision)));

    QList<ArchiveEntryData> entries;
    entries.append({storageInfo.fileName(),
                    QJsonDocument(root).toJson(QJsonDocument::Indented)});
    if (!appendFileIfExists(entries, QFileInfo(projectGoalFilePath(storagePath)).fileName(),
                            projectGoalFilePath(storagePath), error))
    {
        return false;
    }
    if (!appendFileIfExists(entries, QFileInfo(projectActionsLogFilePath(storagePath)).fileName(),
                            projectActionsLogFilePath(storagePath), error))
    {
        return false;
    }
    entries.append({QStringLiteral("manifest.json"),
                    manifestJson(QStringLiteral("project-state"), timestamp, fromRevision,
                                 toRevision, storagePath)});
    return createTarXzArchive(archivePath, entries, error);
}

Revision storedGlobalRevision(QSettings &settings)
{
    return parseRevision(settings.value(kGlobalRevisionKey).toString());
}

Revision storedProjectRevision(const QJsonObject &root)
{
    return parseRevision(root.value(QLatin1StringView(kProjectRevisionKey)).toString());
}

bool saveProjectRoot(const QString &storagePath, const QJsonObject &root, QString *error)
{
    QSaveFile file(storagePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error != nullptr)
            *error = QStringLiteral("Failed to open %1 for writing").arg(storagePath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        if (error != nullptr)
            *error = QStringLiteral("Failed to commit %1").arg(storagePath);
        return false;
    }

    return true;
}

}  // namespace

QString Revision::versionString() const
{
    if (!valid)
        return {};
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString Revision::buildSuffixString() const
{
    if (!valid)
        return {};
    return QStringLiteral("-%1").arg(build, 3, 10, QLatin1Char('0'));
}

QString Revision::revisionString() const
{
    if (!valid)
        return {};
    return versionString() + buildSuffixString();
}

Revision currentRevision()
{
    return currentRevisionImpl();
}

QString currentVersionString()
{
    return QString::fromLatin1(kCurrentVersion);
}

QString currentBuildSuffix()
{
    return QString::fromLatin1(kCurrentBuildSuffix);
}

QString currentRevisionString()
{
    return currentRevision().revisionString();
}

QString globalBackupDirPath()
{
    return ensureGlobalBackupDir();
}

QString projectBackupDirPath(const QString &storagePath)
{
    return ensureProjectBackupDir(storagePath);
}

Revision parseRevision(const QString &version, const QString &buildSuffix)
{
    static const QRegularExpression versionOnlyRe(
        QStringLiteral(R"(^\s*(\d+)\.(\d+)\.(\d+)\s*$)"));
    static const QRegularExpression revisionRe(
        QStringLiteral(R"(^\s*(\d+)\.(\d+)\.(\d+)-(\d+)\s*$)"));
    static const QRegularExpression buildRe(QStringLiteral(R"(^\s*-?(\d+)\s*$)"));

    if (version.trimmed().isEmpty() && buildSuffix.trimmed().isEmpty())
        return {};

    if (buildSuffix.trimmed().isEmpty())
    {
        const auto revisionMatch = revisionRe.match(version);
        if (revisionMatch.hasMatch())
        {
            return {revisionMatch.captured(1).toInt(), revisionMatch.captured(2).toInt(),
                    revisionMatch.captured(3).toInt(), revisionMatch.captured(4).toInt(), true};
        }
    }

    const auto versionMatch = versionOnlyRe.match(version);
    if (!versionMatch.hasMatch())
        return {};

    int build = 0;
    if (!buildSuffix.trimmed().isEmpty())
    {
        const auto buildMatch = buildRe.match(buildSuffix);
        if (!buildMatch.hasMatch())
            return {};
        build = buildMatch.captured(1).toInt();
    }

    return {versionMatch.captured(1).toInt(), versionMatch.captured(2).toInt(),
            versionMatch.captured(3).toInt(), build, true};
}

bool isOlder(const Revision &lhs, const Revision &rhs)
{
    if (!lhs.valid)
        return rhs.valid;
    if (!rhs.valid)
        return false;
    return compare(lhs, rhs) < 0;
}

void stampGlobalSettings(QSettings &settings)
{
    settings.setValue(kGlobalRevisionKey, currentRevisionString());
}

bool migrateGlobalSettings(QSettings &settings, QString *error)
{
    const Revision stored = storedGlobalRevision(settings);
    const Revision current = currentRevision();
    bool changed = false;

    if ((!stored.valid || stored.revisionString() != current.revisionString()) &&
        !settings.allKeys().isEmpty())
    {
        if (!createGlobalSettingsBackup(settings, stored, current, error))
            return false;
    }

    const Revision migration_0_0_4_001 = parseRevision(QStringLiteral("0.0.4"), QStringLiteral("-001"));
    if (isOlder(stored, migration_0_0_4_001))
        changed = migrateGlobalSettingsTo_0_0_4_001(settings) || changed;

    if (!stored.valid || stored.revisionString() != current.revisionString())
    {
        stampGlobalSettings(settings);
        changed = true;
    }

    Q_UNUSED(changed);
    return true;
}

void stampProjectState(QJsonObject &root)
{
    root[QStringLiteral("storageRevision")] = currentRevisionString();
}

bool migrateProjectState(const QString &storagePath, QString *error)
{
    QFile file(storagePath);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error != nullptr)
            *error = QStringLiteral("Failed to open %1 for reading").arg(storagePath);
        return false;
    }

    const QByteArray rawState = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(rawState);
    if (!doc.isObject())
    {
        if (error != nullptr)
            *error = QStringLiteral("Invalid project context JSON: %1").arg(storagePath);
        return false;
    }

    QJsonObject root = doc.object();
    const Revision stored = storedProjectRevision(root);
    const Revision current = currentRevision();
    bool changed = false;

    if (!stored.valid || stored.revisionString() != current.revisionString())
    {
        if (!createProjectStateBackup(storagePath, root, stored, current, error))
            return false;
    }

    const Revision migration_0_0_4_001 = parseRevision(QStringLiteral("0.0.4"), QStringLiteral("-001"));
    if (isOlder(stored, migration_0_0_4_001))
    {
        if (!migrateProjectStateTo_0_0_4_001(storagePath, root, error))
            return false;
        changed = true;
    }

    if (!stored.valid || stored.revisionString() != current.revisionString())
    {
        stampProjectState(root);
        changed = true;
    }

    if (!changed)
        return true;

    return saveProjectRoot(storagePath, root, error);
}

QString projectGoalFilePath(const QString &storagePath)
{
    return siblingFilePath(storagePath, QStringLiteral(".goal.txt"));
}

QString projectActionsLogFilePath(const QString &storagePath)
{
    return siblingFilePath(storagePath, QStringLiteral(".actions-log.md"));
}

}  // namespace qcai2::Migration
