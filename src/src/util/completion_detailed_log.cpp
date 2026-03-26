/*! @file
    @brief Implements detailed per-launch-directory markdown logging for AI completion requests.
*/

#include "completion_detailed_log.h"
#include "system_diagnostics_log.h"

#include "../context/chat_context.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace qcai2
{

namespace
{

QString launch_timestamp_local()
{
    static const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    return timestamp;
}

QString launch_timestamp_for_filename()
{
    static const QString timestamp = []() {
        QString value = launch_timestamp_local();
        value.replace(QLatin1Char(':'), QLatin1Char('-'));
        return value;
    }();
    return timestamp;
}

QString logs_directory_path(const QString &workspace_root)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/logs"));
}

QString safe_file_name_component(QString value)
{
    value = value.trimmed();
    for (QChar &ch : value)
    {
        if (ch.isLetterOrNumber() == true || ch == QLatin1Char('-') || ch == QLatin1Char('_') ||
            ch == QLatin1Char('.'))
        {
            continue;
        }
        ch = QLatin1Char('_');
    }
    return value;
}

QString launch_directory_path(const QString &workspace_root)
{
    return QDir(logs_directory_path(workspace_root))
        .filePath(QStringLiteral("completion-log-%1").arg(launch_timestamp_for_filename()));
}

QString request_log_file_path(const QString &workspace_root, const QString &request_id)
{
    const QString safe_request_id = safe_file_name_component(request_id);
    return QDir(launch_directory_path(workspace_root))
        .filePath(QStringLiteral("%1.md").arg(safe_request_id.isEmpty() == true
                                                  ? QStringLiteral("completion-request")
                                                  : safe_request_id));
}

QString safe_text(const QString &value)
{
    return value.isEmpty() == true ? QStringLiteral("(empty)") : value;
}

QString fenced_block(const QString &text, const QString &language = QStringLiteral("text"))
{
    return QStringLiteral("~~~~%1\n%2\n~~~~\n").arg(language, safe_text(text));
}

QString usage_lines(const provider_usage_t &usage)
{
    return QStringLiteral("- input_tokens: %1\n"
                          "- output_tokens: %2\n"
                          "- total_tokens: %3\n"
                          "- reasoning_tokens: %4\n"
                          "- cached_input_tokens: %5")
        .arg(usage.input_tokens)
        .arg(usage.output_tokens)
        .arg(usage.resolved_total_tokens())
        .arg(usage.reasoning_tokens)
        .arg(usage.cached_input_tokens);
}

QString prompt_part_label(const completion_log_prompt_part_t &part)
{
    return QStringLiteral("%1 — %2 chars / ~%3 tokens")
        .arg(part.role)
        .arg(part.content.size())
        .arg(part.token_estimate);
}

QString file_header_markdown(const QString &workspace_root, const QString &request_id)
{
    QString markdown;
    markdown += QStringLiteral("# qcai2 detailed completion log\n\n");
    markdown += QStringLiteral("- launch_local: `%1`\n").arg(launch_timestamp_local());
    markdown += QStringLiteral("- workspace_root: `%1`\n").arg(workspace_root);
    markdown += QStringLiteral("- request_id: `%1`\n").arg(request_id);
    markdown += QStringLiteral("- format: One Markdown file per completion request inside one "
                               "per-launch directory\n");
    return markdown;
}

}  // namespace

completion_detailed_log_t::completion_detailed_log_t() = default;

void completion_detailed_log_t::begin_request(
    const QString &workspace_root_value, const QString &request_id_value,
    const QString &provider_id_value, const QString &model_value,
    const QString &reasoning_effort_value, const QString &source_file_path_value,
    int cursor_position_value, int prefix_length_value, int suffix_length_value,
    qint64 configured_timeout_ms_value, const QString &prefix_value, const QString &suffix_value,
    const QString &prompt_value, const QList<completion_log_prompt_part_t> &prompt_parts_value)
{
    this->workspace_root = workspace_root_value;
    this->request_id = request_id_value;
    this->provider_id = provider_id_value;
    this->model = model_value;
    this->reasoning_effort = reasoning_effort_value;
    this->source_file_path = source_file_path_value;
    this->cursor_position = cursor_position_value;
    this->prefix_length = prefix_length_value;
    this->suffix_length = suffix_length_value;
    this->configured_timeout_ms = configured_timeout_ms_value;
    this->prefix = prefix_value;
    this->suffix = suffix_value;
    this->prompt = prompt_value;
    this->prompt_parts = prompt_parts_value;
    this->started_at_local = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    this->finished_at_local.clear();
    this->final_status.clear();
    this->response_text.clear();
    this->error_text.clear();
    this->usage = {};
    this->events.clear();
    this->elapsed_timer.invalidate();
    this->elapsed_timer.start();
}

void completion_detailed_log_t::record_stage(const QString &kind, const QString &raw_type,
                                             const QString &message, const QString &tool_name)
{
    completion_log_event_t event;
    event.timestamp_local = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    event.elapsed_ms = this->elapsed_timer.isValid() == true ? this->elapsed_timer.elapsed() : -1;
    event.kind = kind;
    event.raw_type = raw_type;
    event.tool_name = tool_name;
    event.message = message;
    this->events.append(event);
}

void completion_detailed_log_t::record_provider_event(const provider_raw_event_t &event)
{
    this->record_stage(provider_raw_event_kind_name(event.kind), event.raw_type, event.message,
                       event.tool_name);
}

void completion_detailed_log_t::finish_request(const QString &status, const QString &response_text,
                                               const QString &error_text,
                                               const provider_usage_t &usage_value)
{
    this->finished_at_local = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    this->final_status = status;
    this->response_text = response_text;
    this->error_text = error_text;
    this->usage = usage_value;
}

QString completion_detailed_log_t::log_file_path() const
{
    if (this->workspace_root.isEmpty() == true)
    {
        return {};
    }

    return request_log_file_path(this->workspace_root, this->request_id);
}

QString completion_detailed_log_t::workspace_root_path() const
{
    return this->workspace_root;
}

QString completion_detailed_log_t::render_markdown() const
{
    QString markdown;
    markdown += QStringLiteral("## Completion Request `%1`\n\n").arg(this->started_at_local);
    markdown += QStringLiteral("- request_id: `%1`\n").arg(safe_text(this->request_id));
    markdown += QStringLiteral("- status: `%1`\n").arg(safe_text(this->final_status));
    markdown += QStringLiteral("- finished_at_local: `%1`\n")
                    .arg(this->finished_at_local.isEmpty() == true ? QStringLiteral("<pending>")
                                                                   : this->finished_at_local);
    if (this->events.isEmpty() == false)
    {
        markdown +=
            QStringLiteral("- total_duration_ms: `%1`\n")
                .arg(this->events.last().elapsed_ms >= 0 ? this->events.last().elapsed_ms : 0);
    }
    markdown += QStringLiteral("- provider: `%1`\n").arg(safe_text(this->provider_id));
    markdown += QStringLiteral("- model: `%1`\n").arg(safe_text(this->model));
    markdown +=
        QStringLiteral("- reasoning_effort: `%1`\n").arg(safe_text(this->reasoning_effort));
    markdown += QStringLiteral("- source_file: `%1`\n").arg(safe_text(this->source_file_path));
    markdown += QStringLiteral("- cursor_position: `%1`\n").arg(this->cursor_position);
    markdown += QStringLiteral("- prefix_length: `%1`\n").arg(this->prefix_length);
    markdown += QStringLiteral("- suffix_length: `%1`\n").arg(this->suffix_length);
    markdown +=
        QStringLiteral("- configured_timeout_ms: `%1`\n\n")
            .arg(this->configured_timeout_ms > 0 ? QString::number(this->configured_timeout_ms)
                                                 : QStringLiteral("<none>"));

    markdown += QStringLiteral("### Prefix\n\n");
    markdown += fenced_block(this->prefix);

    markdown += QStringLiteral("\n### Suffix\n\n");
    markdown += fenced_block(this->suffix);

    markdown += QStringLiteral("\n### Prompt\n\n");
    markdown += QStringLiteral("- size: %1 chars / ~%2 tokens\n\n")
                    .arg(this->prompt.size())
                    .arg(estimate_token_count(this->prompt));
    markdown += fenced_block(this->prompt);

    markdown += QStringLiteral("\n### Prompt Parts\n\n");
    if (this->prompt_parts.isEmpty() == true)
    {
        markdown += QStringLiteral("(none)\n");
    }
    else
    {
        for (const completion_log_prompt_part_t &part : this->prompt_parts)
        {
            markdown += QStringLiteral("- %1\n\n").arg(prompt_part_label(part));
            markdown += fenced_block(part.content);
        }
    }

    markdown += QStringLiteral("\n### Timeline\n\n");
    if (this->events.isEmpty() == true)
    {
        markdown += QStringLiteral("(no lifecycle events recorded)\n");
    }
    else
    {
        for (const completion_log_event_t &event : this->events)
        {
            markdown += QStringLiteral("- `%1` (+%2 ms) `%3` / `%4`\n")
                            .arg(event.timestamp_local)
                            .arg(event.elapsed_ms >= 0 ? QString::number(event.elapsed_ms)
                                                       : QStringLiteral("?"))
                            .arg(safe_text(event.kind))
                            .arg(safe_text(event.raw_type));
            if (event.tool_name.isEmpty() == false)
            {
                markdown += QStringLiteral("  - tool_name: `%1`\n").arg(event.tool_name);
            }
            if (event.message.isEmpty() == false)
            {
                if (event.message.contains(QLatin1Char('\n')) == true)
                {
                    markdown += QStringLiteral("  - message:\n\n");
                    markdown += fenced_block(event.message);
                }
                else
                {
                    markdown += QStringLiteral("  - message: %1\n").arg(event.message);
                }
            }
        }
    }

    markdown += QStringLiteral("\n### Result\n\n");
    markdown += QStringLiteral("- response_size: %1 chars / ~%2 tokens\n")
                    .arg(this->response_text.size())
                    .arg(estimate_token_count(this->response_text));
    if (this->error_text.isEmpty() == false)
    {
        markdown += QStringLiteral("- error: %1\n").arg(this->error_text);
    }
    markdown += QStringLiteral("\n#### Response Text\n\n");
    markdown += fenced_block(this->response_text);

    markdown += QStringLiteral("\n#### Provider Usage\n\n");
    markdown += usage_lines(this->usage);
    markdown += QStringLiteral("\n");

    return markdown;
}

bool completion_detailed_log_t::append_to_project_log(QString *error) const
{
    const QString path = this->log_file_path();

    append_system_diagnostics_event(this->workspace_root, QStringLiteral("CompletionLog"),
                                    QStringLiteral("append.started"),
                                    QStringLiteral("Preparing completion detailed log write."),
                                    {QStringLiteral("request_id=%1").arg(this->request_id),
                                     QStringLiteral("status=%1").arg(this->final_status),
                                     QStringLiteral("workspace_root=%1").arg(this->workspace_root),
                                     QStringLiteral("target_path=%1").arg(path)});

    if (this->workspace_root.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Detailed completion logging requires a workspace root");
        }
        append_system_diagnostics_event(
            this->workspace_root, QStringLiteral("CompletionLog"),
            QStringLiteral("append.failed.workspace_root_missing"),
            QStringLiteral("Detailed completion logging requires a workspace root."));
        return false;
    }

    const QString directory_path = launch_directory_path(this->workspace_root);
    if (QDir().mkpath(directory_path) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to create completion launch log directory: %1")
                         .arg(directory_path);
        }
        append_system_diagnostics_event(
            this->workspace_root, QStringLiteral("CompletionLog"),
            QStringLiteral("append.failed.mkdir"),
            QStringLiteral("Failed to create completion launch log directory."),
            {QStringLiteral("directory_path=%1").arg(directory_path)});
        return false;
    }
    append_system_diagnostics_event(this->workspace_root, QStringLiteral("CompletionLog"),
                                    QStringLiteral("append.directory_ready"),
                                    QStringLiteral("Completion launch log directory is ready."),
                                    {QStringLiteral("directory_path=%1").arg(directory_path)});
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open completion log file %1: %2")
                         .arg(path, file.errorString());
        }
        append_system_diagnostics_event(this->workspace_root, QStringLiteral("CompletionLog"),
                                        QStringLiteral("append.failed.open"),
                                        QStringLiteral("Failed to open completion log file."),
                                        {QStringLiteral("target_path=%1").arg(path),
                                         QStringLiteral("error=%1").arg(file.errorString())});
        return false;
    }
    append_system_diagnostics_event(this->workspace_root, QStringLiteral("CompletionLog"),
                                    QStringLiteral("append.file_opened"),
                                    QStringLiteral("Opened completion log file for writing."),
                                    {QStringLiteral("target_path=%1").arg(path)});

    QTextStream stream(&file);
    stream << file_header_markdown(this->workspace_root, this->request_id);
    stream << this->render_markdown();
    if (stream.status() != QTextStream::Ok)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to write completion log file %1").arg(path);
        }
        append_system_diagnostics_event(
            this->workspace_root, QStringLiteral("CompletionLog"),
            QStringLiteral("append.failed.write"),
            QStringLiteral("Failed while writing completion log file."),
            {QStringLiteral("target_path=%1").arg(path)});
        return false;
    }

    if (file.commit() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to commit completion log file %1: %2")
                         .arg(path, file.errorString());
        }
        append_system_diagnostics_event(this->workspace_root, QStringLiteral("CompletionLog"),
                                        QStringLiteral("append.failed.commit"),
                                        QStringLiteral("Failed to commit completion log file."),
                                        {QStringLiteral("target_path=%1").arg(path),
                                         QStringLiteral("error=%1").arg(file.errorString())});
        return false;
    }

    append_system_diagnostics_event(
        this->workspace_root, QStringLiteral("CompletionLog"), QStringLiteral("append.succeeded"),
        QStringLiteral("Completion detailed log file written successfully."),
        {QStringLiteral("target_path=%1").arg(path)});

    return true;
}

}  // namespace qcai2
