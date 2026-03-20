#pragma once

#include "../context/editor_context.h"

#include <QFileInfo>
#include <QFutureInterface>
#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <memory>

namespace qcai2
{

class chat_context_manager_t;
class vector_search_backend_t;
struct settings_t;

class vector_indexing_manager_t final : public QObject
{
    Q_OBJECT

public:
    explicit vector_indexing_manager_t(QObject *parent = nullptr);

    void initialize(editor_context_t *editor_context,
                    chat_context_manager_t *chat_context_manager);
    void refresh_from_settings();

    enum class job_kind_t
    {
        FULL_WORKSPACE,
        FILE,
        HISTORY_MESSAGE,
    };

    struct job_t
    {
        job_kind_t kind = job_kind_t::FULL_WORKSPACE;
        QString workspace_id;
        QString workspace_root;
        QStringList project_files;
        QString file_path;
        QString conversation_id;
        QString message_id;
        QString role;
        QString source;
        QString content;
        QString created_at;
    };

    struct job_result_t
    {
        QString error;
        int processed_items = 0;
        bool cancelled = false;
    };

    struct file_state_t
    {
        qint64 modified_ms = 0;
        qint64 size = 0;
    };

    struct index_state_t
    {
        QString backend_base_url;
        QString collection_name;
        QString embedder_version;
        QHash<QString, file_state_t> files;
    };

private:
    void enqueue_full_workspace_job(const QString &workspace_id, const QString &workspace_root,
                                    const QStringList &project_files);
    void enqueue_file_job(const QString &workspace_root, const QString &file_path);
    void enqueue_history_job(const QString &workspace_id, const QString &workspace_root,
                             const QString &conversation_id, const QString &message_id,
                             const QString &role, const QString &source, const QString &content,
                             const QString &created_at);
    void start_next_job();
    void handle_job_finished();

    static QString normalize_workspace_root(const QString &path);
    static QString normalize_file_path(const QString &path);
    static QString workspace_state_path(const QString &workspace_root);
    static QString workspace_session_path(const QString &workspace_root);
    static QString workspace_store_path(const QString &workspace_root);
    static QString workspace_artifacts_path(const QString &workspace_root);
    static QString deterministic_uuid_for_key(const QString &value);
    static QString normalize_exclude_path(const QString &workspace_root, const QString &path);
    static QStringList load_vector_search_excludes(const QString &workspace_root);
    static bool is_excluded_by_root_hidden_directory(const QString &workspace_root,
                                                     const QString &file_path);
    static bool is_excluded_by_session_config(const QString &file_path,
                                              const QStringList &exclude_prefixes);
    static bool is_indexable_text_file(const QFileInfo &file_info);
    static bool read_text_file(const QString &file_path, QString *content, QString *error);
    static file_state_t state_for_file(const QFileInfo &file_info);
    static index_state_t load_index_state(const QString &workspace_root);
    static bool write_index_state(const QString &workspace_root, const index_state_t &state,
                                  QString *error);
    static bool state_matches_backend(const index_state_t &state, const settings_t &settings);
    static QString file_point_id(const QString &workspace_root, const QString &file_path,
                                 int chunk_index);
    static QString history_point_id(const QString &workspace_root, const QString &conversation_id,
                                    const QString &message_id, int chunk_index);

    std::unique_ptr<vector_search_backend_t> create_backend(const settings_t &settings) const;
    job_result_t run_job(const job_t &job, const settings_t &settings,
                         QFutureInterface<void> &future_interface, int generation) const;
    job_result_t run_full_workspace_job(const job_t &job, const settings_t &settings,
                                        QFutureInterface<void> &future_interface,
                                        int generation) const;
    job_result_t run_file_job(const job_t &job, const settings_t &settings,
                              QFutureInterface<void> &future_interface, int generation) const;
    job_result_t run_history_job(const job_t &job, const settings_t &settings,
                                 QFutureInterface<void> &future_interface, int generation) const;
    bool is_cancelled(int generation) const;

    editor_context_t *editor_context = nullptr;
    chat_context_manager_t *chat_context_manager = nullptr;
    QList<job_t> pending_jobs;
    QSet<QString> pending_workspace_roots;
    QSet<QString> pending_file_paths;
    QSet<QString> pending_history_keys;
    QFutureWatcher<void> *current_job_watcher = nullptr;
    job_result_t *current_job_result = nullptr;
    bool initialized = false;
    int cancellation_generation = 0;
};

vector_indexing_manager_t &vector_indexing_manager();

}  // namespace qcai2
