#include "request_detailed_log.h"

#include "../context/chat_context.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>

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

QString launch_log_file_path(const QString &workspace_root)
{
    return QDir(logs_directory_path(workspace_root))
        .filePath(QStringLiteral("request-log-%1.md").arg(launch_timestamp_for_filename()));
}

QString yes_no(bool value)
{
    return value == true ? QStringLiteral("yes") : QStringLiteral("no");
}

QString safe_text(const QString &value)
{
    return value.isEmpty() == true ? QStringLiteral("(empty)") : value;
}

QString fenced_block(const QString &text, const QString &language = QStringLiteral("text"))
{
    return QStringLiteral("~~~~%1\n%2\n~~~~\n").arg(language, safe_text(text));
}

QString json_text(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
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

QString input_part_label(const request_log_input_part_t &part)
{
    return QStringLiteral("%1 — %2 chars / ~%3 tokens")
        .arg(part.title)
        .arg(part.content.size())
        .arg(part.token_estimate);
}

bool is_linked_files_request_context(const QString &request_context,
                                     const QStringList &linked_files)
{
    return linked_files.isEmpty() == false &&
           request_context.startsWith(QStringLiteral("Linked files for this request:\n"));
}

bool is_linked_files_input_part(const request_log_input_part_t &part,
                                const QString &request_context, const QStringList &linked_files)
{
    if (is_linked_files_request_context(request_context, linked_files) == false)
    {
        return false;
    }

    return part.content == request_context ||
           part.content.startsWith(QStringLiteral("Linked files for this request:\n")) ||
           (request_context.isEmpty() == false && part.content.contains(request_context));
}

qint64 total_iteration_input_tokens(const request_log_iteration_t &iteration)
{
    qint64 total = 0;
    for (const request_log_input_part_t &part : iteration.input_parts)
    {
        total += std::max(0, part.token_estimate);
    }
    return total;
}

QString file_header_markdown(const QString &workspace_root)
{
    QString markdown;
    markdown += QStringLiteral("# qcai2 detailed request log\n\n");
    markdown += QStringLiteral("- launch_local: `%1`\n").arg(launch_timestamp_local());
    markdown += QStringLiteral("- workspace_root: `%1`\n").arg(workspace_root);
    markdown += QStringLiteral("- format: Markdown append-only per request\n");
    return markdown;
}

}  // namespace

request_detailed_log_t::request_detailed_log_t() = default;

void request_detailed_log_t::begin_request(
    const QString &workspace_root_value, const QString &run_id_value, const QString &goal_value,
    const QString &request_context_value, const QStringList &linked_files_value,
    const QString &provider_id_value, const QString &model_value,
    const QString &reasoning_effort_value, const QString &thinking_level_value,
    const QString &run_mode_value, bool dry_run_value,
    const QStringList &mcp_refresh_messages_value)
{
    this->workspace_root = workspace_root_value;
    this->run_id = run_id_value;
    this->started_at_local = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    this->finished_at_local.clear();
    this->goal = goal_value;
    this->request_context = request_context_value;
    this->linked_files = linked_files_value;
    this->provider_id = provider_id_value;
    this->model = model_value;
    this->reasoning_effort = reasoning_effort_value;
    this->thinking_level = thinking_level_value;
    this->run_mode = run_mode_value;
    this->dry_run = dry_run_value;
    this->mcp_refresh_messages = mcp_refresh_messages_value;
    this->iterations.clear();
    this->tool_events.clear();
    this->final_status.clear();
    this->final_metadata = {};
    this->accumulated_usage = {};
    this->final_diff.clear();
}

void request_detailed_log_t::set_run_id(const QString &run_id_value)
{
    this->run_id = run_id_value;
}

request_log_iteration_t *request_detailed_log_t::find_or_create_iteration(int iteration)
{
    for (request_log_iteration_t &item : this->iterations)
    {
        if (item.iteration == iteration)
        {
            return &item;
        }
    }

    request_log_iteration_t item;
    item.iteration = iteration;
    this->iterations.append(item);
    return &this->iterations.last();
}

void request_detailed_log_t::record_iteration_input(
    int iteration, const QString &provider_id_value, const QString &model_value,
    const QString &reasoning_effort_value, const QString &thinking_level_value, double temperature,
    int max_tokens, const QList<request_log_input_part_t> &input_parts)
{
    request_log_iteration_t *entry = this->find_or_create_iteration(iteration);
    entry->provider_id = provider_id_value;
    entry->model = model_value;
    entry->reasoning_effort = reasoning_effort_value;
    entry->thinking_level = thinking_level_value;
    entry->temperature = temperature;
    entry->max_tokens = max_tokens;
    entry->input_parts = input_parts;
}

void request_detailed_log_t::record_iteration_output(int iteration, const QString &output_text,
                                                     const QString &error_text,
                                                     const QString &response_type,
                                                     const QString &summary,
                                                     const provider_usage_t &usage,
                                                     qint64 duration_ms)
{
    request_log_iteration_t *entry = this->find_or_create_iteration(iteration);
    entry->output_text = output_text;
    entry->error_text = error_text;
    entry->response_type = response_type;
    entry->summary = summary;
    entry->usage = usage;
    entry->duration_ms = duration_ms;
}

void request_detailed_log_t::record_tool_event(const request_log_tool_event_t &event)
{
    this->tool_events.append(event);
}

void request_detailed_log_t::finish_request(const QString &status, const QJsonObject &metadata,
                                            const provider_usage_t &accumulated_usage_value,
                                            const QString &final_diff_value)
{
    this->finished_at_local = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    this->final_status = status;
    this->final_metadata = metadata;
    this->accumulated_usage = accumulated_usage_value;
    this->final_diff = final_diff_value;
}

QString request_detailed_log_t::log_file_path() const
{
    if (this->workspace_root.isEmpty() == true)
    {
        return {};
    }
    return launch_log_file_path(this->workspace_root);
}

QString request_detailed_log_t::render_markdown() const
{
    QString markdown;
    markdown += QStringLiteral("\n\n---\n\n");
    markdown += QStringLiteral("## Request `%1`\n\n").arg(this->started_at_local);
    markdown += QStringLiteral("- run_id: `%1`\n")
                    .arg(this->run_id.isEmpty() == true ? QStringLiteral("<none>") : this->run_id);
    markdown += QStringLiteral("- status: `%1`\n").arg(this->final_status);
    markdown += QStringLiteral("- finished_at_local: `%1`\n")
                    .arg(this->finished_at_local.isEmpty() == true ? QStringLiteral("<pending>")
                                                                   : this->finished_at_local);
    markdown += QStringLiteral("- mode: `%1`\n").arg(this->run_mode);
    markdown += QStringLiteral("- provider: `%1`\n").arg(this->provider_id);
    markdown += QStringLiteral("- model: `%1`\n").arg(this->model);
    markdown += QStringLiteral("- reasoning_effort: `%1`\n").arg(this->reasoning_effort);
    markdown += QStringLiteral("- thinking_level: `%1`\n").arg(this->thinking_level);
    markdown += QStringLiteral("- dry_run: `%1`\n\n").arg(yes_no(this->dry_run));

    markdown += QStringLiteral("### Goal\n\n");
    markdown += QStringLiteral("- size: %1 chars / ~%2 tokens\n\n")
                    .arg(this->goal.size())
                    .arg(estimate_token_count(this->goal));
    markdown += fenced_block(this->goal);

    markdown += QStringLiteral("\n### Linked Files\n\n");
    if (this->linked_files.isEmpty() == true)
    {
        markdown += QStringLiteral("(none)\n");
    }
    else
    {
        for (const QString &file : this->linked_files)
        {
            markdown += QStringLiteral("- `%1`\n").arg(file);
        }
    }

    markdown += QStringLiteral("\n### Request Context\n\n");
    markdown += QStringLiteral("- size: %1 chars / ~%2 tokens\n\n")
                    .arg(this->request_context.size())
                    .arg(estimate_token_count(this->request_context));
    if (is_linked_files_request_context(this->request_context, this->linked_files) == true)
    {
        markdown += QStringLiteral("- linked_file_count: `%1`\n").arg(this->linked_files.size());
        markdown +=
            QStringLiteral("- content: redacted because linked file contents are not logged\n");
    }
    else
    {
        markdown += fenced_block(this->request_context);
    }

    markdown += QStringLiteral("\n### MCP Refresh\n\n");
    if (this->mcp_refresh_messages.isEmpty() == true)
    {
        markdown += QStringLiteral("(none)\n");
    }
    else
    {
        for (const QString &message : this->mcp_refresh_messages)
        {
            markdown += QStringLiteral("- %1\n").arg(message);
        }
    }

    markdown += QStringLiteral("\n### Iterations\n");
    if (this->iterations.isEmpty() == true)
    {
        markdown += QStringLiteral("\n(no provider requests recorded)\n");
    }
    else
    {
        for (const request_log_iteration_t &iteration : this->iterations)
        {
            markdown += QStringLiteral("\n#### Iteration %1\n\n").arg(iteration.iteration);
            markdown += QStringLiteral("- provider: `%1`\n").arg(iteration.provider_id);
            markdown += QStringLiteral("- model: `%1`\n").arg(iteration.model);
            markdown +=
                QStringLiteral("- reasoning_effort: `%1`\n").arg(iteration.reasoning_effort);
            markdown += QStringLiteral("- thinking_level: `%1`\n").arg(iteration.thinking_level);
            markdown += QStringLiteral("- temperature: `%1`\n").arg(iteration.temperature);
            markdown += QStringLiteral("- max_tokens: `%1`\n").arg(iteration.max_tokens);
            markdown += QStringLiteral("- input_parts: `%1`\n").arg(iteration.input_parts.size());
            markdown += QStringLiteral("- input_estimated_tokens_total: `~%1`\n")
                            .arg(total_iteration_input_tokens(iteration));
            if (iteration.duration_ms >= 0)
            {
                markdown += QStringLiteral("- duration_ms: `%1`\n").arg(iteration.duration_ms);
            }
            markdown +=
                QStringLiteral("- response_type: `%1`\n\n")
                    .arg(iteration.response_type.isEmpty() == true ? QStringLiteral("<unknown>")
                                                                   : iteration.response_type);

            markdown += QStringLiteral("##### Input Parts\n\n");
            if (iteration.input_parts.isEmpty() == true)
            {
                markdown += QStringLiteral("(none)\n");
            }
            else
            {
                for (const request_log_input_part_t &part : iteration.input_parts)
                {
                    markdown += QStringLiteral("- %1\n\n").arg(input_part_label(part));
                    if (is_linked_files_input_part(part, this->request_context,
                                                   this->linked_files) == true)
                    {
                        markdown +=
                            QStringLiteral("_redacted: linked file contents are not logged._\n");
                    }
                    else
                    {
                        markdown += fenced_block(part.content);
                    }
                }
            }

            markdown += QStringLiteral("\n##### Output\n\n");
            markdown += QStringLiteral("- size: %1 chars / ~%2 tokens\n")
                            .arg(iteration.output_text.size())
                            .arg(estimate_token_count(iteration.output_text));
            if (iteration.summary.isEmpty() == false)
            {
                markdown += QStringLiteral("- summary: %1\n").arg(iteration.summary);
            }
            if (iteration.error_text.isEmpty() == false)
            {
                markdown += QStringLiteral("- error: %1\n").arg(iteration.error_text);
            }
            markdown += QStringLiteral("\n");
            markdown += fenced_block(iteration.output_text);

            markdown += QStringLiteral("\n##### Provider Usage\n\n");
            markdown += usage_lines(iteration.usage);
            markdown += QStringLiteral("\n");
        }
    }

    markdown += QStringLiteral("\n### Tools\n");
    if (this->tool_events.isEmpty() == true)
    {
        markdown += QStringLiteral("\n(none)\n");
    }
    else
    {
        for (const request_log_tool_event_t &tool : this->tool_events)
        {
            const QString args_text = json_text(tool.args);
            markdown += QStringLiteral("\n#### `%1` (iteration %2)\n\n")
                            .arg(tool.name)
                            .arg(tool.iteration);
            markdown += QStringLiteral("- mcp: `%1`\n").arg(yes_no(tool.is_mcp));
            markdown +=
                QStringLiteral("- approval_required: `%1`\n").arg(yes_no(tool.approval_required));
            markdown += QStringLiteral("- approved: `%1`\n").arg(yes_no(tool.approved));
            markdown += QStringLiteral("- denied: `%1`\n").arg(yes_no(tool.denied));
            markdown += QStringLiteral("- args_size: %1 chars / ~%2 tokens\n")
                            .arg(args_text.size())
                            .arg(estimate_token_count(args_text));
            markdown += QStringLiteral("- result_size: %1 chars / ~%2 tokens\n\n")
                            .arg(tool.result.size())
                            .arg(estimate_token_count(tool.result));
            markdown += QStringLiteral("##### Arguments\n\n");
            markdown += fenced_block(args_text, QStringLiteral("json"));
            markdown += QStringLiteral("\n##### Result\n\n");
            markdown += fenced_block(tool.result);
        }
    }

    markdown += QStringLiteral("\n### Final State\n\n");
    markdown += QStringLiteral("- total_iterations: `%1`\n").arg(this->iterations.size());
    markdown += QStringLiteral("- total_tool_events: `%1`\n").arg(this->tool_events.size());
    markdown +=
        QStringLiteral("- accumulated_usage:\n%1\n").arg(usage_lines(this->accumulated_usage));

    if (this->final_diff.isEmpty() == false)
    {
        markdown += QStringLiteral("\n### Final Diff\n\n");
        markdown += fenced_block(this->final_diff, QStringLiteral("diff"));
    }

    if (this->final_metadata.isEmpty() == false)
    {
        markdown += QStringLiteral("\n### Final Metadata\n\n");
        markdown += fenced_block(json_text(this->final_metadata), QStringLiteral("json"));
    }

    return markdown;
}

bool request_detailed_log_t::append_to_project_log(QString *error) const
{
    if (this->workspace_root.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Detailed request logging requires a workspace root");
        }
        return false;
    }

    const QString directory_path = logs_directory_path(this->workspace_root);
    if (QDir().mkpath(directory_path) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to create detailed log directory: %1").arg(directory_path);
        }
        return false;
    }

    const QString path = this->log_file_path();
    const bool new_file = QFileInfo::exists(path) == false;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open detailed log file %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    if (new_file == true)
    {
        stream << file_header_markdown(this->workspace_root);
    }
    stream << this->render_markdown();
    if (stream.status() != QTextStream::Ok)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to write detailed log file %1").arg(path);
        }
        return false;
    }

    return true;
}

}  // namespace qcai2
