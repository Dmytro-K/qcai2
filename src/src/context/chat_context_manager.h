#pragma once

#include "../context/editor_context.h"
#include "chat_context_store.h"

#include <QHash>
#include <QObject>

#include <memory>

namespace qcai2
{

struct settings_t;

class chat_context_manager_t : public QObject
{
    Q_OBJECT

public:
    explicit chat_context_manager_t(QObject *parent = nullptr);

    bool set_active_workspace(const QString &workspace_id, const QString &workspace_root,
                              const QString &conversation_id = {}, QString *error = nullptr);
    QString active_workspace_id() const;
    QString active_workspace_root() const;
    QString active_conversation_id() const;

    QString ensure_active_conversation(QString *error = nullptr);
    QString start_new_conversation(const QString &title = {}, QString *error = nullptr);

    void sync_workspace_state(const editor_context_t::snapshot_t &snapshot,
                              const settings_t &settings, QString *error = nullptr);

    QString begin_run(context_request_kind_t kind, const QString &provider_id,
                      const QString &model, const QString &reasoning_effort,
                      const QString &thinking_level, bool dry_run,
                      const QJsonObject &metadata = {}, QString *error = nullptr);
    bool finish_run(const QString &run_id, const QString &status, const provider_usage_t &usage,
                    const QJsonObject &metadata = {}, QString *error = nullptr);

    bool append_user_message(const QString &run_id, const QString &content, const QString &source,
                             const QJsonObject &metadata = {}, QString *error = nullptr);
    bool append_assistant_message(const QString &run_id, const QString &content,
                                  const QString &source, const QJsonObject &metadata = {},
                                  QString *error = nullptr);
    bool append_artifact(const QString &run_id, const QString &kind, const QString &title,
                         const QString &content, const QJsonObject &metadata = {},
                         QString *error = nullptr);

    context_envelope_t build_context_envelope(context_request_kind_t kind,
                                              const QString &system_instruction,
                                              const QStringList &dynamic_system_messages,
                                              int max_output_tokens,
                                              QString *error = nullptr) const;
    QString build_completion_context_block(const QString &file_path, int max_output_tokens,
                                           QString *error = nullptr) const;

    bool maybe_refresh_summary(QString *error = nullptr);

    QString database_path() const;
    QString artifact_directory_path() const;

private:
    bool ensure_store_open(QString *error = nullptr) const;
    bool ensure_conversation_available(QString *error = nullptr);
    bool append_message(const QString &run_id, const QString &role, const QString &content,
                        const QString &source, const QJsonObject &metadata,
                        QString *error = nullptr);

    QString conversation_summary_block(const context_summary_t &summary, int max_tokens) const;
    QString memory_block(const QList<memory_item_t> &items, int max_tokens) const;
    QString artifact_block(const QList<artifact_record_t> &artifacts, int max_tokens) const;
    QString recent_messages_block(const QList<context_message_t> &messages, int max_tokens) const;
    QString render_message_bullet(const context_message_t &message, int max_chars) const;
    QString render_artifact_bullet(const artifact_record_t &artifact, int max_chars) const;
    QList<memory_item_t> select_memory_items(const context_budget_t &budget,
                                             QString *error = nullptr) const;
    QList<artifact_record_t> select_artifacts(const context_budget_t &budget,
                                              QString *error = nullptr) const;
    QList<context_message_t> select_recent_messages(const context_budget_t &budget,
                                                    QString *error = nullptr) const;
    context_summary_t compose_next_summary(const context_summary_t &previous,
                                           const QList<context_message_t> &chunk,
                                           const QList<artifact_record_t> &artifacts,
                                           QString *error = nullptr) const;

    QString database_path_for_workspace() const;
    QString artifact_path_for_workspace() const;

    mutable std::unique_ptr<chat_context_store_t> store;
    QString workspace_id;
    QString workspace_root;
    conversation_record_t conversation;
    QHash<QString, run_record_t> active_runs;
};

}  // namespace qcai2
