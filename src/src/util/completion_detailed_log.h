/*! @file
    @brief Declares detailed per-launch-directory markdown logging for AI completion requests.
*/
#pragma once

#include "../progress/agent_progress.h"
#include "../providers/provider_usage.h"

#include <QElapsedTimer>
#include <QString>

namespace qcai2
{

/**
 * Stores one prompt part logged for a completion request.
 */
struct completion_log_prompt_part_t
{
    QString role;
    QString content;
    int token_estimate = 0;
};

/**
 * Stores one timestamped completion request lifecycle event.
 */
struct completion_log_event_t
{
    QString timestamp_local;
    qint64 elapsed_ms = -1;
    QString kind;
    QString raw_type;
    QString tool_name;
    QString message;
};

/**
 * Accumulates one completion request trace and writes it into a project-local launch directory.
 */
class completion_detailed_log_t
{
public:
    /**
     * Creates one empty completion detailed log.
     */
    completion_detailed_log_t();

    /**
     * Starts logging one completion request.
     * @param workspace_root_value Project/workspace directory used for `.qcai2/logs`.
     * @param request_id_value Stable local completion request identifier.
     * @param provider_id_value Provider identifier.
     * @param model_value Model used for the request.
     * @param reasoning_effort_value Completion reasoning setting.
     * @param source_file_path_value Source file being completed.
     * @param cursor_position_value Cursor position in the document.
     * @param prefix_length_value Prefix length in characters.
     * @param suffix_length_value Suffix length in characters.
     * @param configured_timeout_ms_value Configured timeout in milliseconds, or -1 when not used.
     * @param prefix_value Captured prefix text.
     * @param suffix_value Captured suffix text.
     * @param prompt_value Final user prompt sent to the provider.
     * @param prompt_parts_value Serialized provider message parts.
     */
    void begin_request(const QString &workspace_root_value, const QString &request_id_value,
                       const QString &provider_id_value, const QString &model_value,
                       const QString &reasoning_effort_value,
                       const QString &source_file_path_value, int cursor_position_value,
                       int prefix_length_value, int suffix_length_value,
                       qint64 configured_timeout_ms_value, const QString &prefix_value,
                       const QString &suffix_value, const QString &prompt_value,
                       const QList<completion_log_prompt_part_t> &prompt_parts_value);

    /**
     * Records one local lifecycle stage.
     * @param kind High-level event kind.
     * @param raw_type More specific stage identifier.
     * @param message Optional stage details.
     * @param tool_name Optional tool name when relevant.
     */
    void record_stage(const QString &kind, const QString &raw_type, const QString &message = {},
                      const QString &tool_name = {});

    /**
     * Records one provider-emitted raw progress event.
     * @param event Provider progress event to capture.
     */
    void record_provider_event(const provider_raw_event_t &event);

    /**
     * Finishes the request trace.
     * @param status Final request status such as `success` or `error`.
     * @param response_text Final provider response text.
     * @param error_text Final error text, if any.
     * @param usage_value Final provider usage counters.
     */
    void finish_request(const QString &status, const QString &response_text,
                        const QString &error_text, const provider_usage_t &usage_value);

    /**
     * Writes the rendered trace to the project-local completion log file for this request.
     * @param error Output error string on failure.
     * @return True on success.
     */
    bool append_to_project_log(QString *error) const;

    /**
     * Returns the current completion log file path inside the active launch directory.
     * @return Absolute log file path, or empty when unavailable.
     */
    QString log_file_path() const;

    /**
     * Returns the workspace root used for log path resolution.
     * @return Project/workspace directory used for `.qcai2/logs`.
     */
    QString workspace_root_path() const;

private:
    /**
     * Renders the accumulated request trace as markdown.
     * @return Markdown document written to one per-request file.
     */
    QString render_markdown() const;

    /** Project/workspace directory used for `.qcai2/logs`. */
    QString workspace_root;

    /** Stable local completion request identifier. */
    QString request_id;

    /** Provider identifier used for the request. */
    QString provider_id;

    /** Model name used for the request. */
    QString model;

    /** Completion reasoning setting used for the request. */
    QString reasoning_effort;

    /** Source file path used to build the completion prompt. */
    QString source_file_path;

    /** Cursor position captured for the request. */
    int cursor_position = 0;

    /** Prefix length in characters. */
    int prefix_length = 0;

    /** Suffix length in characters. */
    int suffix_length = 0;

    /** Configured timeout in milliseconds, or -1 when not applicable. */
    qint64 configured_timeout_ms = -1;

    /** Prefix text sent to the prompt builder. */
    QString prefix;

    /** Suffix text sent to the prompt builder. */
    QString suffix;

    /** Final prompt text sent to the provider. */
    QString prompt;

    /** Prompt parts sent to the provider. */
    QList<completion_log_prompt_part_t> prompt_parts;

    /** Absolute local timestamp of request start. */
    QString started_at_local;

    /** Absolute local timestamp of request finish. */
    QString finished_at_local;

    /** Final status string. */
    QString final_status;

    /** Final provider response text. */
    QString response_text;

    /** Final error text. */
    QString error_text;

    /** Final provider usage counters. */
    provider_usage_t usage;

    /** Monotonic timer used to compute per-stage elapsed times. */
    QElapsedTimer elapsed_timer;

    /** Captured lifecycle events in chronological order. */
    QList<completion_log_event_t> events;
};

}  // namespace qcai2
