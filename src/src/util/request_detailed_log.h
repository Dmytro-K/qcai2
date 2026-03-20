#pragma once

#include "../providers/provider_usage.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace qcai2
{

struct request_log_input_part_t
{
    QString title;
    QString content;
    int token_estimate = 0;
};

struct request_log_iteration_t
{
    int iteration = 0;
    QString provider_id;
    QString model;
    QString reasoning_effort;
    QString thinking_level;
    double temperature = 0.0;
    int max_tokens = 0;
    QList<request_log_input_part_t> input_parts;
    QString output_text;
    QString error_text;
    QString response_type;
    QString summary;
    provider_usage_t usage;
    qint64 duration_ms = -1;
};

struct request_log_tool_event_t
{
    int iteration = 0;
    QString name;
    QJsonObject args;
    QString result;
    bool approval_required = false;
    bool approved = false;
    bool denied = false;
    bool is_mcp = false;
};

class request_detailed_log_t
{
public:
    request_detailed_log_t();

    void begin_request(const QString &workspace_root, const QString &run_id, const QString &goal,
                       const QString &request_context, const QStringList &linked_files,
                       const QString &provider_id, const QString &model,
                       const QString &reasoning_effort, const QString &thinking_level,
                       const QString &run_mode, bool dry_run,
                       const QStringList &mcp_refresh_messages);
    void set_run_id(const QString &run_id);
    void record_iteration_input(int iteration, const QString &provider_id, const QString &model,
                                const QString &reasoning_effort, const QString &thinking_level,
                                double temperature, int max_tokens,
                                const QList<request_log_input_part_t> &input_parts);
    void record_iteration_output(int iteration, const QString &output_text,
                                 const QString &error_text, const QString &response_type,
                                 const QString &summary, const provider_usage_t &usage,
                                 qint64 duration_ms);
    void record_tool_event(const request_log_tool_event_t &event);
    void finish_request(const QString &status, const QJsonObject &metadata,
                        const provider_usage_t &accumulated_usage, const QString &final_diff);

    bool append_to_project_log(QString *error) const;
    QString log_file_path() const;

private:
    request_log_iteration_t *find_or_create_iteration(int iteration);
    QString render_markdown() const;

    QString workspace_root;
    QString run_id;
    QString started_at_local;
    QString finished_at_local;
    QString goal;
    QString request_context;
    QStringList linked_files;
    QString provider_id;
    QString model;
    QString reasoning_effort;
    QString thinking_level;
    QString run_mode;
    bool dry_run = true;
    QStringList mcp_refresh_messages;
    QList<request_log_iteration_t> iterations;
    QList<request_log_tool_event_t> tool_events;
    QString final_status;
    QJsonObject final_metadata;
    provider_usage_t accumulated_usage;
    QString final_diff;
};

}  // namespace qcai2
