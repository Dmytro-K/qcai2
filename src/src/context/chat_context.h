#pragma once

#include "../models/agent_messages.h"
#include "../providers/provider_usage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace qcai2
{

struct context_message_t
{
    QString message_id;
    QString workspace_id;
    QString conversation_id;
    QString run_id;
    int sequence = 0;
    QString role;
    QString source;
    QString content;
    QString created_at;
    int token_estimate = 0;
    QJsonObject metadata;

    QJsonObject to_json() const;
    static context_message_t from_json(const QJsonObject &obj);
    bool include_in_prompt() const;
    chat_message_t to_chat_message() const;
};

struct context_summary_t
{
    QString summary_id;
    QString workspace_id;
    QString conversation_id;
    int version = 0;
    int start_sequence = 0;
    int end_sequence = 0;
    QString created_at;
    QString content;
    int token_estimate = 0;
    QJsonObject metadata;

    bool is_valid() const;
    QJsonObject to_json() const;
    static context_summary_t from_json(const QJsonObject &obj);
};

struct memory_item_t
{
    QString memory_id;
    QString workspace_id;
    QString key;
    QString category;
    QString value;
    QString source;
    QString updated_at;
    int importance = 0;
    int token_estimate = 0;
    QJsonObject metadata;

    QJsonObject to_json() const;
    static memory_item_t from_json(const QJsonObject &obj);
};

struct artifact_record_t
{
    QString artifact_id;
    QString workspace_id;
    QString conversation_id;
    QString run_id;
    QString kind;
    QString title;
    QString storage_path;
    QString preview;
    QString created_at;
    int token_estimate = 0;
    QJsonObject metadata;

    QJsonObject to_json() const;
    static artifact_record_t from_json(const QJsonObject &obj);
};

struct conversation_record_t
{
    QString conversation_id;
    QString workspace_id;
    QString workspace_root;
    QString title;
    QString created_at;
    QString updated_at;
    int last_message_sequence = 0;
    QJsonObject metadata;

    bool is_valid() const;
    QJsonObject to_json() const;
    static conversation_record_t from_json(const QJsonObject &obj);
};

struct run_record_t
{
    QString run_id;
    QString workspace_id;
    QString conversation_id;
    QString kind;
    QString provider_id;
    QString model;
    QString reasoning_effort;
    QString thinking_level;
    bool dry_run = true;
    QString status;
    QString started_at;
    QString finished_at;
    provider_usage_t usage;
    QJsonObject metadata;

    QJsonObject to_json() const;
    static run_record_t from_json(const QJsonObject &obj);
};

enum class context_request_kind_t : std::uint8_t
{
    AGENT_CHAT,
    ASK,
    COMPLETION,
};

struct context_budget_t
{
    int total_tokens = 0;
    int memory_tokens = 0;
    int summary_tokens = 0;
    int recent_message_tokens = 0;
    int artifact_tokens = 0;
    int max_recent_messages = 0;
    int max_artifacts = 0;

    QJsonObject to_json() const;
};

struct context_envelope_t
{
    QString workspace_id;
    QString conversation_id;
    context_budget_t budget;
    QList<memory_item_t> memory_items;
    context_summary_t summary;
    QList<context_message_t> recent_messages;
    QList<artifact_record_t> artifacts;
    QList<chat_message_t> provider_messages;

    QJsonObject to_json() const;
};

int estimate_token_count(const QString &text);
QString truncate_for_context(const QString &text, int max_chars);
QString new_context_id();
QString now_utc_iso_string();
QString request_kind_name(context_request_kind_t kind);
context_budget_t budget_for_request(context_request_kind_t kind, int max_output_tokens);
QString default_artifact_file_suffix(const QString &kind);

}  // namespace qcai2
