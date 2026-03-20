#pragma once

#include "chat_context.h"

namespace qcai2
{

class chat_context_store_t
{
public:
    chat_context_store_t();
    ~chat_context_store_t();

    bool open(const QString &database_path, const QString &artifact_dir_path,
              QString *error = nullptr);
    void close();

    bool is_open() const;
    QString database_path() const;
    QString artifact_dir_path() const;

    bool ensure_conversation(conversation_record_t *conversation, QString *error = nullptr);
    conversation_record_t conversation(const QString &conversation_id,
                                       QString *error = nullptr) const;
    QList<conversation_record_t> conversations(const QString &workspace_id,
                                               QString *error = nullptr) const;
    bool delete_conversation(const QString &conversation_id, QString *error = nullptr);

    bool save_run(const run_record_t &run, QString *error = nullptr);
    bool update_run(const run_record_t &run, QString *error = nullptr);

    bool append_message(context_message_t *message, QString *error = nullptr);
    bool upsert_memory_item(memory_item_t *item, QString *error = nullptr);
    bool add_summary(context_summary_t *summary, QString *error = nullptr);
    bool add_artifact(artifact_record_t *artifact, const QString &content,
                      QString *error = nullptr);

    QList<context_message_t> recent_messages(const QString &conversation_id, int limit,
                                             QString *error = nullptr) const;
    QList<context_message_t> messages_range(const QString &conversation_id,
                                            int start_sequence_exclusive,
                                            int end_sequence_inclusive,
                                            QString *error = nullptr) const;
    QList<memory_item_t> memory_items(const QString &workspace_id, int limit,
                                      QString *error = nullptr) const;
    QList<artifact_record_t> recent_artifacts(const QString &conversation_id, int limit,
                                              QString *error = nullptr) const;

    context_summary_t latest_summary(const QString &conversation_id,
                                     QString *error = nullptr) const;
    int latest_summary_version(const QString &conversation_id, QString *error = nullptr) const;
    int latest_message_sequence(const QString &conversation_id, QString *error = nullptr) const;

private:
    bool initialize_layout(QString *error);
    bool ensure_open(QString *error) const;
    QString conversations_directory_path() const;
    QString conversation_directory_path(const QString &conversation_id) const;
    QString conversation_state_path(const QString &conversation_id) const;
    QString conversation_messages_path(const QString &conversation_id) const;
    QString conversation_summaries_path(const QString &conversation_id) const;
    QString conversation_latest_summary_path(const QString &conversation_id) const;
    QString conversation_artifacts_path(const QString &conversation_id) const;
    QString runs_directory_path() const;
    QString run_path(const QString &run_id) const;
    QString memory_path() const;
    QString format_path() const;

    QString database_file_path;
    QString artifacts_directory_path;
    bool open_state = false;
};

}  // namespace qcai2
