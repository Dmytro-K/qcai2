#include "vector_indexing_manager.h"

#include "../context/chat_context_manager.h"
#include "../context/chat_context_store.h"
#include "../settings/settings.h"
#include "../util/logger.h"
#include "text_embedder.h"
#include "vector_search_service.h"

#if QCAI2_FEATURE_QDRANT_ENABLE
#include "qdrant/qdrant_vector_search_backend.h"
#endif

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/idocument.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <utils/filepath.h>
#include <utils/id.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureInterface>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrentRun>

#include <limits>
#include <utility>

namespace qcai2
{

namespace
{

constexpr int upsert_batch_size = 24;
constexpr int indexed_file_size_bytes_max = 512 * 1024;
constexpr int index_state_version = 1;
const Utils::Id vector_index_progress_id("QCAI2.VectorIndex");
const QString embedder_version = QStringLiteral("hashing-256-v1");

QJsonArray float_list_to_json_array(const QList<float> &values)
{
    QJsonArray array;
    for (const float value : values)
    {
        array.append(static_cast<double>(value));
    }
    return array;
}

QJsonObject match_condition(const QString &key, const QString &value)
{
    return QJsonObject{
        {QStringLiteral("key"), key},
        {QStringLiteral("match"), QJsonObject{{QStringLiteral("value"), value}}},
    };
}

QJsonObject qdrant_filter(const std::initializer_list<QPair<QString, QString>> &conditions)
{
    QJsonArray must_conditions;
    for (const auto &condition : conditions)
    {
        must_conditions.append(match_condition(condition.first, condition.second));
    }
    return QJsonObject{{QStringLiteral("must"), must_conditions}};
}

QStringList collect_workspace_files(const QString &workspace_root)
{
    QStringList files;
    if (workspace_root.trimmed().isEmpty() == true)
    {
        return files;
    }

    QDir root_dir(workspace_root);
    const QFileInfoList root_entries = root_dir.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &entry : root_entries)
    {
        if (entry.isDir() == true)
        {
            if (entry.fileName().startsWith(QLatin1Char('.')) == true)
            {
                continue;
            }

            QDirIterator iterator(entry.absoluteFilePath(),
                                  QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext() == true)
            {
                files.append(iterator.next());
            }
        }
        else if (entry.isFile() == true)
        {
            files.append(entry.absoluteFilePath());
        }
    }

    files.removeDuplicates();
    return files;
}

bool flush_points(vector_search_backend_t *backend, QJsonArray *points, int vector_dimensions,
                  QString *error)
{
    if (points == nullptr || points->isEmpty() == true)
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    const QJsonArray batch = *points;
    *points = QJsonArray{};
    return backend->upsert_points(batch, vector_dimensions, error);
}

QJsonObject make_file_point(const QString &point_id, const QList<float> &vector,
                            const QString &workspace_id, const QString &workspace_root,
                            const QString &file_path, const QString &relative_path,
                            const text_chunk_t &chunk)
{
    return QJsonObject{
        {QStringLiteral("id"), point_id},
        {QStringLiteral("vector"), float_list_to_json_array(vector)},
        {QStringLiteral("payload"),
         QJsonObject{
             {QStringLiteral("entity_kind"), QStringLiteral("file")},
             {QStringLiteral("workspace_id"), workspace_id},
             {QStringLiteral("workspace_root"), workspace_root},
             {QStringLiteral("file_path"), file_path},
             {QStringLiteral("relative_path"), relative_path},
             {QStringLiteral("title"), QStringLiteral("%1:%2-%3")
                                           .arg(relative_path)
                                           .arg(chunk.line_start)
                                           .arg(chunk.line_end)},
             {QStringLiteral("content"), chunk.content},
             {QStringLiteral("line_start"), chunk.line_start},
             {QStringLiteral("line_end"), chunk.line_end},
         }},
    };
}

QJsonObject make_history_point(const QString &point_id, const QList<float> &vector,
                               const QString &workspace_id, const QString &workspace_root,
                               const QString &conversation_id, const QString &message_id,
                               const QString &role, const QString &source,
                               const QString &created_at, const text_chunk_t &chunk)
{
    return QJsonObject{
        {QStringLiteral("id"), point_id},
        {QStringLiteral("vector"), float_list_to_json_array(vector)},
        {QStringLiteral("payload"),
         QJsonObject{
             {QStringLiteral("entity_kind"), QStringLiteral("history")},
             {QStringLiteral("workspace_id"), workspace_id},
             {QStringLiteral("workspace_root"), workspace_root},
             {QStringLiteral("conversation_id"), conversation_id},
             {QStringLiteral("message_id"), message_id},
             {QStringLiteral("role"), role},
             {QStringLiteral("source"), source},
             {QStringLiteral("created_at"), created_at},
             {QStringLiteral("title"), QStringLiteral("%1 message").arg(role)},
             {QStringLiteral("content"), chunk.content},
         }},
    };
}

}  // namespace

vector_indexing_manager_t::vector_indexing_manager_t(QObject *parent) : QObject(parent)
{
}

vector_indexing_manager_t &vector_indexing_manager()
{
    static vector_indexing_manager_t manager;
    return manager;
}

void vector_indexing_manager_t::initialize(editor_context_t *editor_context,
                                           chat_context_manager_t *chat_context_manager)
{
    this->editor_context = editor_context;
    this->chat_context_manager = chat_context_manager;
    if (this->initialized == true)
    {
        return;
    }

    QObject::connect(
        Core::EditorManager::instance(), &Core::EditorManager::saved, this,
        [this](Core::IDocument *document, Core::IDocument::SaveOption) {
            if (document == nullptr)
            {
                return;
            }
            const QString workspace_root = normalize_workspace_root(
                ProjectExplorer::ProjectManager::projectForFile(document->filePath()) != nullptr
                    ? ProjectExplorer::ProjectManager::projectForFile(document->filePath())
                          ->projectDirectory()
                          .toUrlishString()
                    : QString());
            if (workspace_root.isEmpty() == true)
            {
                return;
            }
            this->enqueue_file_job(workspace_root, document->filePath().toUrlishString());
        });

    if (this->chat_context_manager != nullptr)
    {
        QObject::connect(
            this->chat_context_manager, &chat_context_manager_t::message_appended, this,
            [this](const QString &workspace_id, const QString &workspace_root,
                   const QString &conversation_id, const QString &message_id, const QString &role,
                   const QString &source, const QString &content, const QString &created_at) {
                this->enqueue_history_job(workspace_id, workspace_root, conversation_id,
                                          message_id, role, source, content, created_at);
            });
    }

    if (ProjectExplorer::ProjectManager::instance() != nullptr)
    {
        QObject::connect(
            ProjectExplorer::ProjectManager::instance(),
            &ProjectExplorer::ProjectManager::projectAdded, this,
            [this](ProjectExplorer::Project *project) {
                if (project == nullptr)
                {
                    return;
                }
                this->enqueue_full_workspace_job(
                    project->projectFilePath().toUrlishString(),
                    project->projectDirectory().toUrlishString(),
                    collect_workspace_files(project->projectDirectory().toUrlishString()));
            });
        QObject::connect(
            ProjectExplorer::ProjectManager::instance(),
            &ProjectExplorer::ProjectManager::startupProjectChanged, this,
            [this](ProjectExplorer::Project *project) {
                if (project == nullptr)
                {
                    return;
                }
                this->enqueue_full_workspace_job(
                    project->projectFilePath().toUrlishString(),
                    project->projectDirectory().toUrlishString(),
                    collect_workspace_files(project->projectDirectory().toUrlishString()));
            });
    }

    this->initialized = true;
}

void vector_indexing_manager_t::refresh_from_settings()
{
    ++this->cancellation_generation;
    this->pending_jobs.clear();
    this->pending_workspace_roots.clear();
    this->pending_file_paths.clear();
    this->pending_history_keys.clear();

    if (this->initialized == false || this->editor_context == nullptr)
    {
        return;
    }

    if (settings().vector_search_enabled == false ||
        vector_search_service().is_available() == false)
    {
        return;
    }

    const QList<editor_context_t::project_info_t> projects = this->editor_context->open_projects();
    if (projects.isEmpty() == true)
    {
        const editor_context_t::snapshot_t snapshot = this->editor_context->capture();
        if (snapshot.project_dir.isEmpty() == false)
        {
            this->enqueue_full_workspace_job(snapshot.project_file_path, snapshot.project_dir,
                                             collect_workspace_files(snapshot.project_dir));
        }
        return;
    }

    for (const editor_context_t::project_info_t &project_info : projects)
    {
        this->enqueue_full_workspace_job(project_info.project_file_path, project_info.project_dir,
                                         collect_workspace_files(project_info.project_dir));
    }
}

void vector_indexing_manager_t::enqueue_full_workspace_job(const QString &workspace_id,
                                                           const QString &workspace_root,
                                                           const QStringList &project_files)
{
    const QString normalized_root = normalize_workspace_root(workspace_root);
    if (normalized_root.isEmpty() == true)
    {
        return;
    }
    if (settings().vector_search_enabled == false ||
        vector_search_service().is_available() == false)
    {
        return;
    }
    if (this->pending_workspace_roots.contains(normalized_root) == true)
    {
        return;
    }

    job_t job;
    job.kind = job_kind_t::FULL_WORKSPACE;
    job.workspace_id = workspace_id;
    job.workspace_root = normalized_root;
    job.project_files = project_files;
    this->pending_jobs.append(job);
    this->pending_workspace_roots.insert(normalized_root);
    this->start_next_job();
}

void vector_indexing_manager_t::enqueue_file_job(const QString &workspace_root,
                                                 const QString &file_path)
{
    const QString normalized_root = normalize_workspace_root(workspace_root);
    const QString normalized_file = normalize_file_path(file_path);
    if (normalized_root.isEmpty() == true || normalized_file.isEmpty() == true)
    {
        return;
    }
    if (settings().vector_search_enabled == false ||
        vector_search_service().is_available() == false)
    {
        return;
    }
    if (this->pending_workspace_roots.contains(normalized_root) == true ||
        this->pending_file_paths.contains(normalized_file) == true)
    {
        return;
    }

    job_t job;
    job.kind = job_kind_t::FILE;
    job.workspace_root = normalized_root;
    job.file_path = normalized_file;
    this->pending_jobs.append(job);
    this->pending_file_paths.insert(normalized_file);
    this->start_next_job();
}

void vector_indexing_manager_t::enqueue_history_job(const QString &workspace_id,
                                                    const QString &workspace_root,
                                                    const QString &conversation_id,
                                                    const QString &message_id, const QString &role,
                                                    const QString &source, const QString &content,
                                                    const QString &created_at)
{
    const QString normalized_root = normalize_workspace_root(workspace_root);
    if (normalized_root.isEmpty() == true || message_id.isEmpty() == true ||
        content.isEmpty() == true)
    {
        return;
    }
    if (settings().vector_search_enabled == false ||
        vector_search_service().is_available() == false)
    {
        return;
    }

    const QString key = normalized_root + QLatin1Char(':') + message_id;
    if (this->pending_workspace_roots.contains(normalized_root) == true ||
        this->pending_history_keys.contains(key) == true)
    {
        return;
    }

    job_t job;
    job.kind = job_kind_t::HISTORY_MESSAGE;
    job.workspace_id = workspace_id;
    job.workspace_root = normalized_root;
    job.conversation_id = conversation_id;
    job.message_id = message_id;
    job.role = role;
    job.source = source;
    job.content = content;
    job.created_at = created_at;
    this->pending_jobs.append(job);
    this->pending_history_keys.insert(key);
    this->start_next_job();
}

void vector_indexing_manager_t::start_next_job()
{
    if (this->current_job_watcher != nullptr || this->pending_jobs.isEmpty() == true)
    {
        return;
    }

    const job_t job = this->pending_jobs.takeFirst();
    switch (job.kind)
    {
        case job_kind_t::FULL_WORKSPACE:
            this->pending_workspace_roots.remove(job.workspace_root);
            break;
        case job_kind_t::FILE:
            this->pending_file_paths.remove(job.file_path);
            break;
        case job_kind_t::HISTORY_MESSAGE:
            this->pending_history_keys.remove(job.workspace_root + QLatin1Char(':') +
                                              job.message_id);
            break;
    }

    this->current_job_result = new job_result_t();
    auto *future_interface = new QFutureInterface<void>();
    future_interface->reportStarted();
    future_interface->setProgressRange(0, 0);

    const QString title =
        job.kind == job_kind_t::FULL_WORKSPACE ? QStringLiteral("Indexing project vector data")
        : job.kind == job_kind_t::FILE         ? QStringLiteral("Updating vector index for %1")
                                                     .arg(QFileInfo(job.file_path).fileName())
                                               : QStringLiteral("Indexing chat history");
    Core::ProgressManager::addTask(future_interface->future(), title, vector_index_progress_id,
                                   Core::ProgressManager::KeepOnFinish);

    this->current_job_watcher = new QFutureWatcher<void>(this);
    const int generation = this->cancellation_generation;
    const settings_t settings_snapshot = settings();

    QObject::connect(this->current_job_watcher, &QFutureWatcher<void>::finished, this,
                     [this, future_interface]() {
                         future_interface->reportFinished();
                         this->handle_job_finished();
                         delete future_interface;
                     });

    this->current_job_watcher->setFuture(
        QtConcurrent::run([this, job, settings_snapshot, future_interface, generation]() {
            *this->current_job_result =
                this->run_job(job, settings_snapshot, *future_interface, generation);
        }));
}

void vector_indexing_manager_t::handle_job_finished()
{
    if (this->current_job_result != nullptr)
    {
        if (this->current_job_result->error.isEmpty() == false)
        {
            QCAI_WARN("VectorIndex", this->current_job_result->error);
        }
        else if (this->current_job_result->cancelled == false &&
                 this->current_job_result->processed_items > 0)
        {
            QCAI_INFO("VectorIndex", QStringLiteral("Indexed %1 vector items")
                                         .arg(this->current_job_result->processed_items));
        }
        delete this->current_job_result;
        this->current_job_result = nullptr;
    }

    if (this->current_job_watcher != nullptr)
    {
        this->current_job_watcher->deleteLater();
        this->current_job_watcher = nullptr;
    }

    this->start_next_job();
}

QString vector_indexing_manager_t::normalize_workspace_root(const QString &path)
{
    if (path.trimmed().isEmpty() == true)
    {
        return {};
    }
    const QFileInfo file_info(path);
    const QString canonical = file_info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(file_info.absoluteFilePath()) : canonical;
}

QString vector_indexing_manager_t::normalize_file_path(const QString &path)
{
    if (path.trimmed().isEmpty() == true)
    {
        return {};
    }
    const QFileInfo file_info(path);
    const QString canonical = file_info.canonicalFilePath();
    if (canonical.isEmpty() == false)
    {
        return canonical;
    }
    return QDir::cleanPath(file_info.absoluteFilePath());
}

QString vector_indexing_manager_t::workspace_state_path(const QString &workspace_root)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/vector-index.json"));
}

QString vector_indexing_manager_t::workspace_session_path(const QString &workspace_root)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/session.json"));
}

QString vector_indexing_manager_t::workspace_store_path(const QString &workspace_root)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/chat-context"));
}

QString vector_indexing_manager_t::workspace_artifacts_path(const QString &workspace_root)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/artifacts"));
}

QString vector_indexing_manager_t::deterministic_uuid_for_key(const QString &value)
{
    QByteArray uuid_bytes =
        QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).left(16);

    uchar version_byte = static_cast<uchar>(uuid_bytes.at(6));
    version_byte = static_cast<uchar>((version_byte & 0x0F) | 0x50);
    uuid_bytes[6] = static_cast<char>(version_byte);

    uchar variant_byte = static_cast<uchar>(uuid_bytes.at(8));
    variant_byte = static_cast<uchar>((variant_byte & 0x3F) | 0x80);
    uuid_bytes[8] = static_cast<char>(variant_byte);

    return QUuid::fromRfc4122(uuid_bytes).toString(QUuid::WithoutBraces);
}

QString vector_indexing_manager_t::normalize_exclude_path(const QString &workspace_root,
                                                          const QString &path)
{
    const QString trimmed_path = QDir::fromNativeSeparators(path.trimmed());
    if (trimmed_path.isEmpty() == true)
    {
        return {};
    }

    const QString normalized_root = normalize_workspace_root(workspace_root);
    QString resolved_path = trimmed_path;
    if (QDir::isRelativePath(resolved_path) == true && normalized_root.isEmpty() == false)
    {
        resolved_path = QDir(normalized_root).absoluteFilePath(resolved_path);
    }

    return QDir::cleanPath(QFileInfo(resolved_path).absoluteFilePath());
}

QStringList vector_indexing_manager_t::load_vector_search_excludes(const QString &workspace_root)
{
    QStringList excludes;

    QFile file(workspace_session_path(workspace_root));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        return excludes;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject() == false)
    {
        return excludes;
    }

    const QJsonObject vector_search_root =
        document.object().value(QStringLiteral("vector_search")).toObject();
    const QJsonArray exclude_array = vector_search_root.value(QStringLiteral("exclude")).toArray();
    for (const QJsonValue &entry : exclude_array)
    {
        if (entry.isString() == false)
        {
            continue;
        }

        const QString normalized = normalize_exclude_path(workspace_root, entry.toString());
        if (normalized.isEmpty() == false)
        {
            excludes.append(normalized);
        }
    }

    excludes.removeDuplicates();
    return excludes;
}

bool vector_indexing_manager_t::is_excluded_by_root_hidden_directory(const QString &workspace_root,
                                                                     const QString &file_path)
{
    const QString normalized_root = normalize_workspace_root(workspace_root);
    const QString normalized_file = normalize_file_path(file_path);
    if (normalized_root.isEmpty() == true || normalized_file.isEmpty() == true)
    {
        return false;
    }

    const QString relative_path = QDir(normalized_root).relativeFilePath(normalized_file);
    if (relative_path.startsWith(QStringLiteral("../")) == true ||
        relative_path == QStringLiteral(".."))
    {
        return false;
    }

    const QStringList segments = relative_path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.size() < 2)
    {
        return false;
    }

    return segments.constFirst().startsWith(QLatin1Char('.'));
}

bool vector_indexing_manager_t::is_excluded_by_session_config(const QString &file_path,
                                                              const QStringList &exclude_prefixes)
{
    const QString normalized_file = normalize_file_path(file_path);
    if (normalized_file.isEmpty() == true)
    {
        return false;
    }

    for (const QString &prefix : exclude_prefixes)
    {
        if (normalized_file.startsWith(prefix) == true)
        {
            return true;
        }
    }
    return false;
}

bool vector_indexing_manager_t::is_indexable_text_file(const QFileInfo &file_info)
{
    if (file_info.exists() == false || file_info.isFile() == false ||
        file_info.size() > indexed_file_size_bytes_max)
    {
        return false;
    }

    QFile file(file_info.absoluteFilePath());
    if (file.open(QIODevice::ReadOnly) == false)
    {
        return false;
    }

    const QByteArray sample = file.read(4096);
    return sample.contains('\0') == false;
}

bool vector_indexing_manager_t::read_text_file(const QString &file_path, QString *content,
                                               QString *error)
{
    if (content == nullptr)
    {
        return false;
    }

    QFile file(file_path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open %1: %2").arg(file_path, file.errorString());
        }
        return false;
    }

    const QByteArray data = file.readAll();
    if (data.contains('\0') == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Skipping binary-looking file %1").arg(file_path);
        }
        return false;
    }

    *content = QString::fromUtf8(data);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

vector_indexing_manager_t::file_state_t
vector_indexing_manager_t::state_for_file(const QFileInfo &file_info)
{
    file_state_t state;
    state.modified_ms = file_info.lastModified().toMSecsSinceEpoch();
    state.size = file_info.size();
    return state;
}

vector_indexing_manager_t::index_state_t
vector_indexing_manager_t::load_index_state(const QString &workspace_root)
{
    index_state_t state;
    QFile file(workspace_state_path(workspace_root));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        return state;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject() == false)
    {
        return state;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != index_state_version)
    {
        return state;
    }

    state.backend_base_url = root.value(QStringLiteral("backendBaseUrl")).toString();
    state.collection_name = root.value(QStringLiteral("collectionName")).toString();
    state.embedder_version = root.value(QStringLiteral("embedderVersion")).toString();

    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
    {
        const QJsonObject file_object = it.value().toObject();
        file_state_t file_state;
        file_state.modified_ms =
            static_cast<qint64>(file_object.value(QStringLiteral("modifiedMs")).toDouble());
        file_state.size =
            static_cast<qint64>(file_object.value(QStringLiteral("size")).toDouble());
        state.files.insert(it.key(), file_state);
    }

    return state;
}

bool vector_indexing_manager_t::write_index_state(const QString &workspace_root,
                                                  const index_state_t &state, QString *error)
{
    QDir root_dir(workspace_root);
    if (root_dir.mkpath(QStringLiteral(".qcai2")) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to create .qcai2 directory under %1").arg(workspace_root);
        }
        return false;
    }

    QJsonObject files_object;
    for (auto it = state.files.begin(); it != state.files.end(); ++it)
    {
        files_object.insert(
            it.key(),
            QJsonObject{
                {QStringLiteral("modifiedMs"), static_cast<double>(it.value().modified_ms)},
                {QStringLiteral("size"), static_cast<double>(it.value().size)},
            });
    }

    const QJsonObject root{
        {QStringLiteral("version"), index_state_version},
        {QStringLiteral("backendBaseUrl"), state.backend_base_url},
        {QStringLiteral("collectionName"), state.collection_name},
        {QStringLiteral("embedderVersion"), state.embedder_version},
        {QStringLiteral("files"), files_object},
    };

    QSaveFile file(workspace_state_path(workspace_root));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open vector index state for %1: %2")
                         .arg(workspace_root, file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to commit vector index state for %1").arg(workspace_root);
        }
        return false;
    }
    return true;
}

bool vector_indexing_manager_t::state_matches_backend(const index_state_t &state,
                                                      const settings_t &settings)
{
    return state.backend_base_url == settings.qdrant_url.trimmed() &&
           state.collection_name == settings.qdrant_collection_name.trimmed() &&
           state.embedder_version == embedder_version;
}

QString vector_indexing_manager_t::file_point_id(const QString &workspace_root,
                                                 const QString &file_path, int chunk_index)
{
    return deterministic_uuid_for_key(
        QStringLiteral("file\n%1\n%2\n%3").arg(workspace_root, file_path).arg(chunk_index));
}

QString vector_indexing_manager_t::history_point_id(const QString &workspace_root,
                                                    const QString &conversation_id,
                                                    const QString &message_id, int chunk_index)
{
    return deterministic_uuid_for_key(QStringLiteral("history\n%1\n%2\n%3\n%4")
                                          .arg(workspace_root, conversation_id, message_id)
                                          .arg(chunk_index));
}

std::unique_ptr<vector_search_backend_t>
vector_indexing_manager_t::create_backend(const settings_t &settings) const
{
#if QCAI2_FEATURE_QDRANT_ENABLE
    if (settings.vector_search_provider == QStringLiteral("qdrant"))
    {
        return std::make_unique<qdrant_vector_search_backend_t>(settings);
    }
#else
    Q_UNUSED(settings);
#endif
    return {};
}

vector_indexing_manager_t::job_result_t
vector_indexing_manager_t::run_job(const job_t &job, const settings_t &settings,
                                   QFutureInterface<void> &future_interface, int generation) const
{
    switch (job.kind)
    {
        case job_kind_t::FULL_WORKSPACE:
            return this->run_full_workspace_job(job, settings, future_interface, generation);
        case job_kind_t::FILE:
            return this->run_file_job(job, settings, future_interface, generation);
        case job_kind_t::HISTORY_MESSAGE:
            return this->run_history_job(job, settings, future_interface, generation);
    }

    job_result_t result;
    result.error = QStringLiteral("Unknown vector indexing job kind.");
    return result;
}

vector_indexing_manager_t::job_result_t
vector_indexing_manager_t::run_full_workspace_job(const job_t &job, const settings_t &settings,
                                                  QFutureInterface<void> &future_interface,
                                                  int generation) const
{
    job_result_t result;
    std::unique_ptr<vector_search_backend_t> backend = this->create_backend(settings);
    if (backend == nullptr)
    {
        result.error = QStringLiteral("Vector search backend is unavailable for indexing.");
        return result;
    }

    QString backend_error;
    if (backend->check_connection(&backend_error) == false)
    {
        result.error = backend_error;
        return result;
    }

    index_state_t index_state = load_index_state(job.workspace_root);
    const QStringList exclude_prefixes = load_vector_search_excludes(job.workspace_root);
    const bool full_reset = state_matches_backend(index_state, settings) == false;
    index_state.backend_base_url = settings.qdrant_url.trimmed();
    index_state.collection_name = settings.qdrant_collection_name.trimmed();
    index_state.embedder_version = embedder_version;

    QStringList current_files;
    current_files.reserve(job.project_files.size());
    for (const QString &candidate : job.project_files)
    {
        const QString normalized = normalize_file_path(candidate);
        if (normalized.isEmpty() == false &&
            is_excluded_by_session_config(normalized, exclude_prefixes) == false)
        {
            current_files.append(normalized);
        }
    }
    current_files.removeDuplicates();

    QStringList files_to_index;
    QStringList files_to_delete;
    if (full_reset == true)
    {
        files_to_index = current_files;
        index_state.files.clear();
    }
    else
    {
        const QSet<QString> current_file_set(current_files.begin(), current_files.end());
        for (auto it = index_state.files.begin(); it != index_state.files.end(); ++it)
        {
            if (current_file_set.contains(it.key()) == false)
            {
                files_to_delete.append(it.key());
            }
        }

        for (const QString &file_path : current_files)
        {
            const QFileInfo file_info(file_path);
            if (is_indexable_text_file(file_info) == false)
            {
                continue;
            }
            const file_state_t candidate_state = state_for_file(file_info);
            if (index_state.files.contains(file_path) == false ||
                index_state.files.value(file_path).modified_ms != candidate_state.modified_ms ||
                index_state.files.value(file_path).size != candidate_state.size)
            {
                files_to_index.append(file_path);
            }
        }
    }

    chat_context_store_t store;
    QList<context_message_t> history_messages;
    QString store_error;
    if (job.workspace_id.isEmpty() == false &&
        store.open(workspace_store_path(job.workspace_root),
                   workspace_artifacts_path(job.workspace_root), &store_error) == true)
    {
        const QList<conversation_record_t> conversations =
            store.conversations(job.workspace_id, &store_error);
        if (store_error.isEmpty() == true)
        {
            for (const conversation_record_t &conversation : conversations)
            {
                const QList<context_message_t> messages =
                    store.messages_range(conversation.conversation_id, 0,
                                         std::numeric_limits<int>::max(), &store_error);
                if (store_error.isEmpty() == false)
                {
                    break;
                }
                history_messages.append(messages);
            }
        }
    }

    if (store_error.isEmpty() == false)
    {
        QCAI_WARN("VectorIndex", store_error);
    }

    const int total_steps = (full_reset ? 1 : 0) + static_cast<int>(files_to_delete.size()) +
                            static_cast<int>(files_to_index.size()) + 1 +
                            static_cast<int>(history_messages.size());
    future_interface.setProgressRange(0, qMax(1, total_steps));
    int completed_steps = 0;

    if (full_reset == true)
    {
        future_interface.setProgressValueAndText(
            completed_steps,
            QStringLiteral("Resetting indexed project files for %1").arg(job.workspace_root));
        if (backend->delete_points(
                qdrant_filter({{QStringLiteral("entity_kind"), QStringLiteral("file")},
                               {QStringLiteral("workspace_root"), job.workspace_root}}),
                &backend_error) == false)
        {
            result.error = backend_error;
            return result;
        }
        future_interface.setProgressValue(++completed_steps);
    }

    for (const QString &file_path : files_to_delete)
    {
        if (this->is_cancelled(generation) == true)
        {
            result.cancelled = true;
            return result;
        }
        future_interface.setProgressValueAndText(
            completed_steps, QStringLiteral("Removing stale vector data for %1").arg(file_path));
        if (backend->delete_points(
                qdrant_filter({{QStringLiteral("entity_kind"), QStringLiteral("file")},
                               {QStringLiteral("workspace_root"), job.workspace_root},
                               {QStringLiteral("file_path"), file_path}}),
                &backend_error) == false)
        {
            result.error = backend_error;
            return result;
        }
        index_state.files.remove(file_path);
        future_interface.setProgressValue(++completed_steps);
    }

    struct prepared_file_index_t
    {
        QString file_path;
        file_state_t file_state;
        QJsonArray points;
        QString warning;
        bool remove_from_state = false;
        bool should_upload = false;
        bool cancelled = false;
    };

    const auto prepare_file = [this, &job, generation](const QString &file_path) {
        prepared_file_index_t prepared;
        prepared.file_path = file_path;
        if (this->is_cancelled(generation) == true)
        {
            prepared.cancelled = true;
            return prepared;
        }

        const QFileInfo file_info(file_path);
        if (is_excluded_by_root_hidden_directory(job.workspace_root, file_path) == true)
        {
            prepared.remove_from_state = true;
            return prepared;
        }

        if (is_indexable_text_file(file_info) == false)
        {
            prepared.remove_from_state = true;
            return prepared;
        }

        QString content;
        QString read_error;
        if (read_text_file(file_path, &content, &read_error) == false)
        {
            prepared.warning = read_error;
            prepared.remove_from_state = true;
            return prepared;
        }

        if (this->is_cancelled(generation) == true)
        {
            prepared.cancelled = true;
            return prepared;
        }

        text_embedder_t embedder;
        const QString relative_path = QDir(job.workspace_root).relativeFilePath(file_path);
        const QList<text_chunk_t> chunks = embedder.chunk_text(content);
        for (int index = 0; index < chunks.size(); ++index)
        {
            prepared.points.append(
                make_file_point(file_point_id(job.workspace_root, file_path, index),
                                embedder.embed_text(chunks.at(index).content), job.workspace_id,
                                job.workspace_root, file_path, relative_path, chunks.at(index)));
        }

        prepared.file_state = state_for_file(file_info);
        prepared.should_upload = true;
        return prepared;
    };

    const int file_count = static_cast<int>(files_to_index.size());
    const int worker_count =
        qMin(qMax(1, settings.vector_search_max_indexing_threads), qMax(1, file_count));
    if (files_to_index.isEmpty() == false)
    {
        future_interface.setProgressValueAndText(
            completed_steps, QStringLiteral("Preparing %1 files with up to %2 threads")
                                 .arg(file_count)
                                 .arg(worker_count));
    }

    QThreadPool preparation_pool;
    preparation_pool.setMaxThreadCount(worker_count);

    QList<QFuture<prepared_file_index_t>> active_futures;
    int next_file_index = 0;
    const auto launch_next_file_tasks = [&]() {
        while (next_file_index < file_count &&
               active_futures.size() < static_cast<qsizetype>(worker_count))
        {
            active_futures.append(QtConcurrent::run(&preparation_pool, prepare_file,
                                                    files_to_index.at(next_file_index)));
            ++next_file_index;
        }
    };
    launch_next_file_tasks();

    while (active_futures.isEmpty() == false)
    {
        if (this->is_cancelled(generation) == true)
        {
            result.cancelled = true;
            return result;
        }

        bool processed_result = false;
        for (qsizetype future_index = active_futures.size(); future_index > 0;)
        {
            --future_index;
            if (active_futures.at(future_index).isFinished() == false)
            {
                continue;
            }

            processed_result = true;
            const prepared_file_index_t prepared = active_futures.takeAt(future_index).result();
            if (prepared.cancelled == true || this->is_cancelled(generation) == true)
            {
                result.cancelled = true;
                return result;
            }

            future_interface.setProgressValueAndText(
                completed_steps, QStringLiteral("Indexing %1").arg(prepared.file_path));
            if (prepared.warning.isEmpty() == false)
            {
                QCAI_WARN("VectorIndex", prepared.warning);
            }

            if (prepared.should_upload == true)
            {
                if (backend->delete_points(
                        qdrant_filter({{QStringLiteral("entity_kind"), QStringLiteral("file")},
                                       {QStringLiteral("workspace_root"), job.workspace_root},
                                       {QStringLiteral("file_path"), prepared.file_path}}),
                        &backend_error) == false)
                {
                    result.error = backend_error;
                    return result;
                }

                QJsonArray batch_points;
                for (const QJsonValue &point : prepared.points)
                {
                    batch_points.append(point);
                    if (batch_points.size() >= upsert_batch_size &&
                        flush_points(backend.get(), &batch_points,
                                     text_embedder_t::embedding_dimensions,
                                     &backend_error) == false)
                    {
                        result.error = backend_error;
                        return result;
                    }
                }
                if (flush_points(backend.get(), &batch_points,
                                 text_embedder_t::embedding_dimensions, &backend_error) == false)
                {
                    result.error = backend_error;
                    return result;
                }

                index_state.files.insert(prepared.file_path, prepared.file_state);
                ++result.processed_items;
            }
            else if (prepared.remove_from_state == true)
            {
                index_state.files.remove(prepared.file_path);
            }

            future_interface.setProgressValue(++completed_steps);
            launch_next_file_tasks();
        }

        if (processed_result == false)
        {
            active_futures.first().waitForFinished();
        }
    }

    future_interface.setProgressValueAndText(completed_steps,
                                             QStringLiteral("Refreshing chat history vectors"));
    if (backend->delete_points(
            qdrant_filter({{QStringLiteral("entity_kind"), QStringLiteral("history")},
                           {QStringLiteral("workspace_root"), job.workspace_root}}),
            &backend_error) == false)
    {
        result.error = backend_error;
        return result;
    }
    future_interface.setProgressValue(++completed_steps);

    text_embedder_t embedder;
    QJsonArray batch_points;
    for (const context_message_t &message : history_messages)
    {
        if (this->is_cancelled(generation) == true)
        {
            result.cancelled = true;
            return result;
        }

        future_interface.setProgressValueAndText(
            completed_steps, QStringLiteral("Indexing chat history %1").arg(message.message_id));

        const QList<text_chunk_t> chunks = embedder.chunk_text(message.content, 1200, 3);
        for (int index = 0; index < chunks.size(); ++index)
        {
            const QList<float> vector = embedder.embed_text(chunks.at(index).content);
            batch_points.append(
                make_history_point(history_point_id(job.workspace_root, message.conversation_id,
                                                    message.message_id, index),
                                   vector, message.workspace_id, job.workspace_root,
                                   message.conversation_id, message.message_id, message.role,
                                   message.source, message.created_at, chunks.at(index)));
            if (batch_points.size() >= upsert_batch_size &&
                flush_points(backend.get(), &batch_points, text_embedder_t::embedding_dimensions,
                             &backend_error) == false)
            {
                result.error = backend_error;
                return result;
            }
        }
        if (flush_points(backend.get(), &batch_points, text_embedder_t::embedding_dimensions,
                         &backend_error) == false)
        {
            result.error = backend_error;
            return result;
        }
        ++result.processed_items;
        future_interface.setProgressValue(++completed_steps);
    }

    QString write_error;
    if (write_index_state(job.workspace_root, index_state, &write_error) == false)
    {
        result.error = write_error;
        return result;
    }

    future_interface.setProgressValue(total_steps);
    return result;
}

vector_indexing_manager_t::job_result_t
vector_indexing_manager_t::run_file_job(const job_t &job, const settings_t &settings,
                                        QFutureInterface<void> &future_interface,
                                        int generation) const
{
    Q_UNUSED(generation);

    job_result_t result;
    std::unique_ptr<vector_search_backend_t> backend = this->create_backend(settings);
    if (backend == nullptr)
    {
        result.error = QStringLiteral("Vector search backend is unavailable for file indexing.");
        return result;
    }

    QString backend_error;
    if (backend->check_connection(&backend_error) == false)
    {
        result.error = backend_error;
        return result;
    }

    future_interface.setProgressRange(0, 1);
    future_interface.setProgressValueAndText(0, QStringLiteral("Updating %1").arg(job.file_path));

    index_state_t index_state = load_index_state(job.workspace_root);
    const QStringList exclude_prefixes = load_vector_search_excludes(job.workspace_root);
    index_state.backend_base_url = settings.qdrant_url.trimmed();
    index_state.collection_name = settings.qdrant_collection_name.trimmed();
    index_state.embedder_version = embedder_version;

    const QFileInfo file_info(job.file_path);
    if (backend->delete_points(
            qdrant_filter({{QStringLiteral("entity_kind"), QStringLiteral("file")},
                           {QStringLiteral("workspace_root"), job.workspace_root},
                           {QStringLiteral("file_path"), job.file_path}}),
            &backend_error) == false)
    {
        result.error = backend_error;
        return result;
    }

    if (is_excluded_by_root_hidden_directory(job.workspace_root, job.file_path) == true ||
        is_excluded_by_session_config(job.file_path, exclude_prefixes) == true)
    {
        index_state.files.remove(job.file_path);
    }
    else if (is_indexable_text_file(file_info) == true)
    {
        QString content;
        QString read_error;
        if (read_text_file(job.file_path, &content, &read_error) == false)
        {
            result.error = read_error;
            return result;
        }

        text_embedder_t embedder;
        const QString relative_path = QDir(job.workspace_root).relativeFilePath(job.file_path);
        const QList<text_chunk_t> chunks = embedder.chunk_text(content);
        QJsonArray points;
        for (int index = 0; index < chunks.size(); ++index)
        {
            points.append(make_file_point(file_point_id(job.workspace_root, job.file_path, index),
                                          embedder.embed_text(chunks.at(index).content), QString(),
                                          job.workspace_root, job.file_path, relative_path,
                                          chunks.at(index)));
        }
        if (backend->upsert_points(points, text_embedder_t::embedding_dimensions,
                                   &backend_error) == false)
        {
            result.error = backend_error;
            return result;
        }

        index_state.files.insert(job.file_path, state_for_file(file_info));
        result.processed_items = 1;
    }
    else
    {
        index_state.files.remove(job.file_path);
    }

    QString write_error;
    if (write_index_state(job.workspace_root, index_state, &write_error) == false)
    {
        result.error = write_error;
        return result;
    }

    future_interface.setProgressValue(1);
    return result;
}

vector_indexing_manager_t::job_result_t
vector_indexing_manager_t::run_history_job(const job_t &job, const settings_t &settings,
                                           QFutureInterface<void> &future_interface,
                                           int generation) const
{
    Q_UNUSED(generation);

    job_result_t result;
    std::unique_ptr<vector_search_backend_t> backend = this->create_backend(settings);
    if (backend == nullptr)
    {
        result.error =
            QStringLiteral("Vector search backend is unavailable for history indexing.");
        return result;
    }

    QString backend_error;
    if (backend->check_connection(&backend_error) == false)
    {
        result.error = backend_error;
        return result;
    }

    future_interface.setProgressRange(0, 1);
    future_interface.setProgressValueAndText(
        0, QStringLiteral("Indexing chat message %1").arg(job.message_id));

    if (backend->delete_points(
            qdrant_filter({{QStringLiteral("entity_kind"), QStringLiteral("history")},
                           {QStringLiteral("workspace_root"), job.workspace_root},
                           {QStringLiteral("message_id"), job.message_id}}),
            &backend_error) == false)
    {
        result.error = backend_error;
        return result;
    }

    text_embedder_t embedder;
    const QList<text_chunk_t> chunks = embedder.chunk_text(job.content, 1200, 3);
    QJsonArray points;
    for (int index = 0; index < chunks.size(); ++index)
    {
        points.append(make_history_point(
            history_point_id(job.workspace_root, job.conversation_id, job.message_id, index),
            embedder.embed_text(chunks.at(index).content), job.workspace_id, job.workspace_root,
            job.conversation_id, job.message_id, job.role, job.source, job.created_at,
            chunks.at(index)));
    }

    if (backend->upsert_points(points, text_embedder_t::embedding_dimensions, &backend_error) ==
        false)
    {
        result.error = backend_error;
        return result;
    }

    result.processed_items = static_cast<int>(points.size());
    future_interface.setProgressValue(1);
    return result;
}

bool vector_indexing_manager_t::is_cancelled(int generation) const
{
    return generation != this->cancellation_generation;
}

}  // namespace qcai2
