/*! Implements revision-aware migrations for plugin settings and project state. */

#include "migration.h"
#include "migrations/migration_0_0_4_001.h"
#include "migrations/migration_0_0_5_002.h"
#include "migrations/migration_0_0_5_003.h"
#include "migrations/migration_0_0_7_001.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cerrno>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
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

constexpr auto global_revision_key = "settingsRevision";

constexpr auto project_revision_key = "storageRevision";

constexpr auto current_version_value = QCAI2_CURRENT_VERSION;
constexpr auto current_build_suffix_value = QCAI2_CURRENT_BUILD_SUFFIX;

struct archive_entry_data_t
{
    QString path;
    QByteArray data;
};

struct archive_write_context_t
{
    QFile file;
};

revision_t current_revision_impl()
{
    return parse_revision(QString::fromLatin1(current_version_value),
                          QString::fromLatin1(current_build_suffix_value));
}

int compare(const revision_t &lhs, const revision_t &rhs)
{
    if (((lhs.major != rhs.major) == true))
    {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (((lhs.minor != rhs.minor) == true))
    {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (((lhs.patch != rhs.patch) == true))
    {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    if (((lhs.build != rhs.build) == true))
    {
        return lhs.build < rhs.build ? -1 : 1;
    }
    return 0;
}

QString read_text_file(const QString &path, bool *exists = nullptr, QString *error = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (((exists != nullptr) == true))
        {
            *exists = false;
        }
        return {};
    }

    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (((exists != nullptr) == true))
        {
            *exists = true;
        }
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open %1 for reading").arg(path);
        }
        return {};
    }

    if (((exists != nullptr) == true))
    {
        *exists = true;
    }
    return QString::fromUtf8(file.readAll());
}

QString sibling_file_path(const QString &storage_path, const QString &suffix)
{
    const QFileInfo storage_info(storage_path);
    return storage_info.dir().filePath(storage_info.completeBaseName() + suffix);
}

QString project_context_dir_path(const QString &storage_path)
{
    return QFileInfo(storage_path).absolutePath();
}

QString project_rules_file_path(const QString &storage_path)
{
    const QString storage_dir_path = QFileInfo(storage_path).absolutePath();
    if (storage_dir_path.isEmpty() == true)
    {
        return {};
    }

    const QString project_dir_path =
        QDir::cleanPath(QDir(storage_dir_path).filePath(QStringLiteral("..")));
    return QDir(project_dir_path).filePath(QStringLiteral(".qcai2/rules.md"));
}

bool normalize_project_prompt_defaults(QJsonObject *root)
{
    if (root == nullptr)
    {
        return false;
    }

    const QJsonValue ignore_value = root->value(QStringLiteral("ignoreGlobalSystemPrompt"));
    const bool ignore_global_system_prompt = ignore_value.toBool(false);
    const bool already_present = root->contains(QStringLiteral("ignoreGlobalSystemPrompt"));
    (*root)[QStringLiteral("ignoreGlobalSystemPrompt")] = ignore_global_system_prompt;
    return already_present == false || ignore_value.isBool() == false;
}

QString revision_label(const revision_t &revision)
{
    return revision.valid ? revision.revision_string() : QStringLiteral("unversioned");
}

QString iso8601_basic_timestamp_utc()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

bool check_archive_result(struct archive *archive_handle, int result, QString *error)
{
    if (((result == ARCHIVE_OK) == true))
    {
        return true;
    }

    if (((error != nullptr) == true))
    {
        *error = QStringLiteral("libarchive error: %1")
                     .arg(QString::fromUtf8(archive_error_string(archive_handle)));
    }
    return false;
}

void set_archive_qt_error(struct archive *archive_handle, const QString &message)
{
    const QByteArray utf8_message = message.toUtf8();
    archive_set_error(archive_handle, EIO, "%s", utf8_message.constData());
}

int archive_open_callback(struct archive *archive_handle, void *client_data)
{
    auto *context = static_cast<archive_write_context_t *>(client_data);
    if (context->file.isOpen())
    {
        return ARCHIVE_OK;
    }

    if (((!context->file.open(QIODevice::WriteOnly)) == true))
    {
        set_archive_qt_error(archive_handle,
                             QStringLiteral("Failed to open %1 for writing: %2")
                                 .arg(context->file.fileName(), context->file.errorString()));
        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

la_ssize_t archive_write_callback(struct archive *archive_handle, void *client_data,
                                  const void *buffer, size_t length)
{
    auto *context = static_cast<archive_write_context_t *>(client_data);
    const qint64 written =
        context->file.write(static_cast<const char *>(buffer), static_cast<qint64>(length));
    if (written < 0 || static_cast<size_t>(written) != length)
    {
        set_archive_qt_error(archive_handle,
                             QStringLiteral("Failed to write %1: %2")
                                 .arg(context->file.fileName(), context->file.errorString()));
        return -1;
    }

    return static_cast<la_ssize_t>(written);
}

int archive_close_callback(struct archive *archive_handle, void *client_data)
{
    auto *context = static_cast<archive_write_context_t *>(client_data);
    if (((!context->file.isOpen()) == true))
    {
        return ARCHIVE_OK;
    }

    if (((!context->file.flush()) == true))
    {
        set_archive_qt_error(archive_handle,
                             QStringLiteral("Failed to flush %1: %2")
                                 .arg(context->file.fileName(), context->file.errorString()));
        context->file.close();
        return ARCHIVE_FATAL;
    }

    context->file.close();
    return ARCHIVE_OK;
}

bool create_tar_xz_archive(const QString &archive_path, const QList<archive_entry_data_t> &entries,
                           QString *error)
{
    struct archive *archive_handle = archive_write_new();
    if (((archive_handle == nullptr) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to allocate libarchive writer");
        }
        return false;
    }

    archive_write_context_t context{QFile(archive_path)};
    bool ok =
        check_archive_result(archive_handle, archive_write_add_filter_xz(archive_handle), error) &&
        check_archive_result(archive_handle,
                             archive_write_set_format_pax_restricted(archive_handle), error) &&
        check_archive_result(archive_handle,
                             archive_write_open(archive_handle, &context, archive_open_callback,
                                                archive_write_callback, archive_close_callback),
                             error);

    for (const archive_entry_data_t &entry_data : entries)
    {
        if (!ok)
        {
            break;
        }

        struct archive_entry *entry = archive_entry_new();
        const QByteArray encoded_entry_path = QFile::encodeName(entry_data.path);
        archive_entry_copy_pathname(entry, encoded_entry_path.constData());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, entry_data.data.size());
        archive_entry_set_mtime(entry, QDateTime::currentSecsSinceEpoch(), 0);

        ok = check_archive_result(archive_handle, archive_write_header(archive_handle, entry),
                                  error);
        if (ok && !entry_data.data.isEmpty())
        {
            const size_t data_size = static_cast<size_t>(entry_data.data.size());
            const la_ssize_t written =
                archive_write_data(archive_handle, entry_data.data.constData(), data_size);
            if (written < 0 || static_cast<size_t>(written) != data_size)
            {
                if (error != nullptr)
                {
                    *error = QStringLiteral("Failed to write archive entry %1: %2")
                                 .arg(entry_data.path,
                                      QString::fromUtf8(archive_error_string(archive_handle)));
                }
                ok = false;
            }
        }

        archive_entry_free(entry);
    }

    const bool close_ok =
        ok && check_archive_result(archive_handle, archive_write_close(archive_handle), error);
    const int free_result = archive_write_free(archive_handle);
    bool free_ok = free_result == ARCHIVE_OK;
    if (((!free_ok && (error != nullptr)) == true))
    {
        *error = QStringLiteral("Failed to finalize archive %1").arg(archive_path);
    }
    return ok && close_ok && free_ok;
}

QString ensure_global_backup_dir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty() == true)
    {
        base = QDir::homePath();
    }
    return QDir(base).filePath(QStringLiteral("qcai2_buk"));
}

QString global_structured_settings_path()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty() == true)
    {
        base = QDir::homePath();
    }
    return QDir(base).filePath(QStringLiteral("settings.json"));
}

QString ensure_project_backup_dir(const QString &storage_path)
{
    QDir context_dir = QFileInfo(storage_path).dir();
    if (context_dir.cdUp() == true)
    {
        return context_dir.filePath(QStringLiteral(".qcai2_buk"));
    }
    return context_dir.filePath(QStringLiteral(".qcai2_buk"));
}

bool ensure_dir_exists(const QString &dir_path, QString *error)
{
    if (((QDir().mkpath(dir_path)) == true))
    {
        return true;
    }
    if (((error != nullptr) == true))
    {
        *error = QStringLiteral("Failed to create backup directory %1").arg(dir_path);
    }
    return false;
}

QByteArray manifest_json(const QString &kind, const QString &created_at,
                         const revision_t &from_revision, const revision_t &to_revision,
                         const QString &source_id)
{
    const QJsonObject manifest{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("createdAt"), created_at},
        {QStringLiteral("fromRevision"), revision_label(from_revision)},
        {QStringLiteral("toRevision"), revision_label(to_revision)},
        {QStringLiteral("source"), source_id},
    };
    return QJsonDocument(manifest).toJson(QJsonDocument::Indented);
}

bool append_file_if_exists(QList<archive_entry_data_t> &entries, const QString &archive_name,
                           const QString &disk_path, QString *error);

bool append_raw_file_if_exists(QList<archive_entry_data_t> &entries, const QString &archive_name,
                               const QString &disk_path, QString *error)
{
    QFile file(disk_path);
    if (file.exists() == false)
    {
        return true;
    }

    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open %1 for reading: %2")
                         .arg(disk_path, file.errorString());
        }
        return false;
    }

    entries.append({archive_name, file.readAll()});
    return true;
}

bool append_directory_tree(QList<archive_entry_data_t> &entries, const QString &archive_root,
                           const QString &disk_dir_path, QString *error)
{
    const QFileInfo dir_info(disk_dir_path);
    if (dir_info.exists() == false)
    {
        return true;
    }
    if (dir_info.isDir() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Expected directory at %1").arg(disk_dir_path);
        }
        return false;
    }

    QStringList file_paths;
    QDirIterator iterator(disk_dir_path, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext() == true)
    {
        file_paths.append(iterator.next());
    }
    std::sort(file_paths.begin(), file_paths.end());

    const QDir root_dir(disk_dir_path);
    for (const QString &file_path : file_paths)
    {
        const QString relative_path = root_dir.relativeFilePath(file_path);
        const QString archive_path = QDir(archive_root).filePath(relative_path);
        if (append_raw_file_if_exists(entries, archive_path, file_path, error) == false)
        {
            return false;
        }
    }

    return true;
}

QJsonObject settings_snapshot(QSettings &settings)
{
    QJsonObject snapshot;
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys)
    {
        snapshot.insert(key, QJsonValue::fromVariant(settings.value(key)));
    }
    return snapshot;
}

bool create_global_settings_backup(QSettings &settings, const revision_t &from_revision,
                                   const revision_t &to_revision, QString *error)
{
    const QString backup_dir = ensure_global_backup_dir();
    if (ensure_dir_exists(backup_dir, error) == false)
    {
        return false;
    }

    const QString timestamp = iso8601_basic_timestamp_utc();
    const QString archive_path =
        QDir(backup_dir)
            .filePath(
                QStringLiteral("global-settings__%1__%2_to_%3.tar.xz")
                    .arg(timestamp, revision_label(from_revision), revision_label(to_revision)));

    QList<archive_entry_data_t> entries;
    entries.append({QStringLiteral("settings.json"),
                    QJsonDocument(settings_snapshot(settings)).toJson(QJsonDocument::Indented)});
    if (((!append_file_if_exists(entries, QStringLiteral("structured-settings.json"),
                                 global_structured_settings_path(), error)) == true))
    {
        return false;
    }
    entries.append({QStringLiteral("manifest.json"),
                    manifest_json(QStringLiteral("global-settings"), timestamp, from_revision,
                                  to_revision, QStringLiteral("QSettings/qcai2"))});
    return create_tar_xz_archive(archive_path, entries, error);
}

bool append_file_if_exists(QList<archive_entry_data_t> &entries, const QString &archive_name,
                           const QString &disk_path, QString *error)
{
    bool exists = false;
    QString read_error;
    const QString text = read_text_file(disk_path, &exists, &read_error);
    if (read_error.isEmpty() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = read_error;
        }
        return false;
    }
    if (exists == false)
    {
        return true;
    }
    entries.append({archive_name, text.toUtf8()});
    return true;
}

bool create_project_state_backup(const QString &storage_path, const revision_t &from_revision,
                                 const revision_t &to_revision, QString *error)
{
    const QString backup_dir = ensure_project_backup_dir(storage_path);
    if (ensure_dir_exists(backup_dir, error) == false)
    {
        return false;
    }

    const QFileInfo storage_info(storage_path);
    const QFileInfo context_dir_info(project_context_dir_path(storage_path));
    const QString timestamp = iso8601_basic_timestamp_utc();
    QString archive_stem = context_dir_info.fileName();
    if (archive_stem.startsWith(QLatin1Char('.')) == true)
    {
        archive_stem.remove(0, 1);
    }
    if (archive_stem.isEmpty() == true)
    {
        archive_stem = storage_info.completeBaseName();
    }
    const QString archive_path =
        QDir(backup_dir)
            .filePath(QStringLiteral("%1__%2__%3_to_%4.tar.xz")
                          .arg(archive_stem, timestamp, revision_label(from_revision),
                               revision_label(to_revision)));

    QList<archive_entry_data_t> entries;
    if (append_directory_tree(entries, context_dir_info.fileName(),
                              context_dir_info.absoluteFilePath(), error) == false)
    {
        return false;
    }
    entries.append({QStringLiteral("manifest.json"),
                    manifest_json(QStringLiteral("project-state"), timestamp, from_revision,
                                  to_revision, context_dir_info.absoluteFilePath())});
    return create_tar_xz_archive(archive_path, entries, error);
}

revision_t stored_global_revision(QSettings &settings)
{
    return parse_revision(settings.value(global_revision_key).toString());
}

revision_t stored_project_revision(const QJsonObject &root)
{
    return parse_revision(root.value(QLatin1StringView(project_revision_key)).toString());
}

bool save_project_root(const QString &storage_path, const QJsonObject &root, QString *error)
{
    const QFileInfo file_info(storage_path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to create directory for %1").arg(storage_path);
        }
        return false;
    }

    QSaveFile file(storage_path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open %1 for writing").arg(storage_path);
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to commit %1").arg(storage_path);
        }
        return false;
    }

    return true;
}

bool ensure_project_prompt_defaults(const QString &storage_path, QJsonObject *root, QString *error)
{
    const bool root_changed = normalize_project_prompt_defaults(root);
    const QString rules_path = project_rules_file_path(storage_path);
    if (rules_path.isEmpty() == true)
    {
        return root_changed;
    }

    QFile file(rules_path);
    if (file.exists() == true)
    {
        return root_changed;
    }

    const QFileInfo file_info(rules_path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to create directory for %1").arg(rules_path);
        }
        return false;
    }

    QSaveFile save_file(rules_path);
    if (save_file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open %1 for writing: %2")
                         .arg(rules_path, save_file.errorString());
        }
        return false;
    }

    if (save_file.commit() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to commit %1").arg(rules_path);
        }
        return false;
    }

    return true;
}

}  // namespace

QString revision_t::version_string() const
{
    if (valid == false)
    {
        return {};
    }
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString revision_t::build_suffix_string() const
{
    if (valid == false)
    {
        return {};
    }
    return QStringLiteral("-%1").arg(build, 3, 10, QLatin1Char('0'));
}

QString revision_t::revision_string() const
{
    if (valid == false)
    {
        return {};
    }
    return this->version_string() + this->build_suffix_string();
}

revision_t current_revision()
{
    return current_revision_impl();
}

QString current_version_string()
{
    return QString::fromLatin1(current_version_value);
}

QString current_build_suffix()
{
    return QString::fromLatin1(current_build_suffix_value);
}

QString current_revision_string()
{
    return current_revision().revision_string();
}

QString global_backup_dir_path()
{
    return ensure_global_backup_dir();
}

QString global_structured_settings_file_path()
{
    return global_structured_settings_path();
}

QString project_backup_dir_path(const QString &storage_path)
{
    return ensure_project_backup_dir(storage_path);
}

QString project_conversations_dir_path(const QString &storage_path)
{
    return QDir(QFileInfo(storage_path).absolutePath()).filePath(QStringLiteral("conversations"));
}

QString conversation_log_journal_file_path(const QString &storage_path,
                                           const QString &conversation_id)
{
    return QDir(project_conversations_dir_path(storage_path))
        .filePath(QStringLiteral("%1.actions-log.jsonl").arg(conversation_id));
}

revision_t parse_revision(const QString &version, const QString &build_suffix)
{
    static const QRegularExpression version_only_re(
        QStringLiteral(R"(^\s*(\d+)\.(\d+)\.(\d+)\s*$)"));
    static const QRegularExpression revision_re(
        QStringLiteral(R"(^\s*(\d+)\.(\d+)\.(\d+)-(\d+)\s*$)"));
    static const QRegularExpression build_re(QStringLiteral(R"(^\s*-?(\d+)\s*$)"));

    if (((version.trimmed().isEmpty() && build_suffix.trimmed().isEmpty()) == true))
    {
        return {};
    }

    if (((build_suffix.trimmed().isEmpty()) == true))
    {
        const auto revision_match = revision_re.match(version);
        if (revision_match.hasMatch())
        {
            return {revision_match.captured(1).toInt(), revision_match.captured(2).toInt(),
                    revision_match.captured(3).toInt(), revision_match.captured(4).toInt(), true};
        }
    }

    const auto version_match = version_only_re.match(version);
    if (((!version_match.hasMatch()) == true))
    {
        return {};
    }

    int build = 0;
    if (((!build_suffix.trimmed().isEmpty()) == true))
    {
        const auto build_match = build_re.match(build_suffix);
        if (((!build_match.hasMatch()) == true))
        {
            return {};
        }
        build = build_match.captured(1).toInt();
    }

    return {version_match.captured(1).toInt(), version_match.captured(2).toInt(),
            version_match.captured(3).toInt(), build, true};
}

bool is_older(const revision_t &lhs, const revision_t &rhs)
{
    if (lhs.valid == false)
    {
        return rhs.valid;
    }
    if (rhs.valid == false)
    {
        return false;
    }
    return compare(lhs, rhs) < 0;
}

void stamp_global_settings(QSettings &settings)
{
    settings.setValue(global_revision_key, current_revision_string());
}

bool migrate_global_settings(QSettings &settings, QString *error)
{
    const revision_t stored = stored_global_revision(settings);
    const revision_t current = current_revision();
    bool changed = false;

    if ((!stored.valid || stored.revision_string() != current.revision_string()) &&
        !settings.allKeys().isEmpty())
    {
        if (create_global_settings_backup(settings, stored, current, error) == false)
        {
            return false;
        }
    }

    const revision_t migration_0_0_4_001 =
        parse_revision(QStringLiteral("0.0.4"), QStringLiteral("-001"));
    if (is_older(stored, migration_0_0_4_001) == true)
    {
        changed = migrate_global_settings_to_0_0_4_001(settings) || changed;
    }

    const revision_t migration_0_0_5_003 =
        parse_revision(QStringLiteral("0.0.5"), QStringLiteral("-003"));
    if (is_older(stored, migration_0_0_5_003) == true)
    {
        changed = migrate_global_settings_to_0_0_5_003(settings) || changed;
    }

    if (((!stored.valid || stored.revision_string() != current.revision_string()) == true))
    {
        stamp_global_settings(settings);
        changed = true;
    }

    Q_UNUSED(changed);
    return true;
}

void stamp_project_state(QJsonObject &root)
{
    root[QStringLiteral("storageRevision")] = current_revision_string();
}

bool migrate_project_state(const QString &storage_path, QString *error)
{
    QFile file(storage_path);
    if (file.exists() == false)
    {
        QJsonObject root;
        stamp_project_state(root);
        if (ensure_project_prompt_defaults(storage_path, &root, error) == false &&
            error != nullptr && error->isEmpty() == false)
        {
            return false;
        }
        return save_project_root(storage_path, root, error);
    }
    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open %1 for reading").arg(storage_path);
        }
        return false;
    }

    const QByteArray raw_state = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw_state);
    if (doc.isObject() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Invalid project context JSON: %1").arg(storage_path);
        }
        return false;
    }

    QJsonObject root = doc.object();
    const revision_t stored = stored_project_revision(root);
    const revision_t current = current_revision();
    const bool needs_0_0_7_001_repair =
        project_state_needs_migration_to_0_0_7_001(storage_path, root);
    bool changed = false;

    if (((!stored.valid || stored.revision_string() != current.revision_string()) == true) ||
        needs_0_0_7_001_repair == true)
    {
        if (create_project_state_backup(storage_path, stored, current, error) == false)
        {
            return false;
        }
    }

    const revision_t migration_0_0_4_001 =
        parse_revision(QStringLiteral("0.0.4"), QStringLiteral("-001"));
    if (is_older(stored, migration_0_0_4_001) == true)
    {
        if (migrate_project_state_to_0_0_4_001(storage_path, root, error) == false)
        {
            return false;
        }
        changed = true;
    }

    const revision_t migration_0_0_5_002 =
        parse_revision(QStringLiteral("0.0.5"), QStringLiteral("-002"));
    if (is_older(stored, migration_0_0_5_002) == true)
    {
        changed = migrate_project_state_to_0_0_5_002(root) || changed;
    }

    const revision_t migration_0_0_5_003 =
        parse_revision(QStringLiteral("0.0.5"), QStringLiteral("-003"));
    if (is_older(stored, migration_0_0_5_003) == true)
    {
        changed = migrate_project_state_to_0_0_5_003(root) || changed;
    }

    const revision_t migration_0_0_7_001 =
        parse_revision(QStringLiteral("0.0.7"), QStringLiteral("-001"));
    if (is_older(stored, migration_0_0_7_001) == true || needs_0_0_7_001_repair == true)
    {
        if (migrate_project_state_to_0_0_7_001(storage_path, root, error) == false)
        {
            return false;
        }
        changed = true;
    }

    changed = normalize_conversation_log_storage_to_0_0_7_001(storage_path, error) || changed;
    if (error != nullptr && error->isEmpty() == false)
    {
        return false;
    }

    if (((!stored.valid || stored.revision_string() != current.revision_string()) == true))
    {
        stamp_project_state(root);
        changed = true;
    }

    changed = ensure_project_prompt_defaults(storage_path, &root, error) || changed;
    if (error != nullptr && error->isEmpty() == false)
    {
        return false;
    }

    if (changed == false)
    {
        return true;
    }

    return save_project_root(storage_path, root, error);
}

QString project_goal_file_path(const QString &storage_path)
{
    return sibling_file_path(storage_path, QStringLiteral(".goal.txt"));
}

QString project_actions_log_file_path(const QString &storage_path)
{
    return sibling_file_path(storage_path, QStringLiteral(".actions-log.md"));
}

QString conversation_state_file_path(const QString &storage_path, const QString &conversation_id)
{
    return QDir(project_conversations_dir_path(storage_path))
        .filePath(QStringLiteral("%1.json").arg(conversation_id));
}

}  // namespace qcai2::Migration
