#include "chat_context_manager.h"

#include "../completion/completion_request_settings.h"
#include "../settings/settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace qcai2
{

namespace
{

constexpr int kSummaryRefreshMessageThreshold = 12;
constexpr int kSummaryRefreshTokenThreshold = 900;
constexpr int kSummaryChunkMaxMessages = 24;
constexpr int kSummaryChunkMaxTokens = 1400;
constexpr int kSummaryCarryForwardChars = 1800;
constexpr int kSummaryMaxChars = 4200;

struct usage_stats_row_t
{
    QString date_local;
    QString hour_local;
    QString tz;
    QString provider;
    QString model;
    QString request_kind;
    bool dry_run = false;
    qint64 request_count = 0;
    qint64 completed_count = 0;
    qint64 non_completed_count = 0;
    qint64 usage_reported_count = 0;
    qint64 input_tokens = 0;
    qint64 output_tokens = 0;
    qint64 total_tokens = 0;
    qint64 reasoning_tokens = 0;
    qint64 cached_input_tokens = 0;
    qint64 duration_ms_total = 0;

    QString key() const
    {
        return date_local + QLatin1Char('|') + hour_local + QLatin1Char('|') + tz +
               QLatin1Char('|') + provider + QLatin1Char('|') + model + QLatin1Char('|') +
               request_kind + QLatin1Char('|') +
               (dry_run == true ? QStringLiteral("true") : QStringLiteral("false"));
    }
};

QString qcai2_path_for_workspace(const QString &workspace_root, const QString &file_name)
{
    return QDir(workspace_root).filePath(QStringLiteral(".qcai2/%1").arg(file_name));
}

QString known_or_unknown(const QString &value)
{
    return value.isEmpty() == true ? QStringLiteral("<unknown>") : value;
}

QJsonObject merge_json_objects(const QJsonObject &base, const QJsonObject &extra)
{
    QJsonObject merged = base;
    for (auto it = extra.begin(); it != extra.end(); ++it)
    {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

memory_item_t make_memory_item(const QString &workspace_id, const QString &key,
                               const QString &category, const QString &value,
                               const QString &source, int importance,
                               const QJsonObject &metadata = {})
{
    memory_item_t item;
    item.workspace_id = workspace_id;
    item.key = key;
    item.category = category;
    item.value = value;
    item.source = source;
    item.importance = importance;
    item.metadata = metadata;
    return item;
}

bool propagate_error(QString *target, const QString &error)
{
    if (error.isEmpty() == true)
    {
        return false;
    }
    if (target != nullptr)
    {
        *target = error;
    }
    return true;
}

const QStringList &usage_stats_header_columns()
{
    static const QStringList columns = {
        QStringLiteral("date_local"),
        QStringLiteral("hour_local"),
        QStringLiteral("tz"),
        QStringLiteral("provider"),
        QStringLiteral("model"),
        QStringLiteral("request_kind"),
        QStringLiteral("dry_run"),
        QStringLiteral("request_count"),
        QStringLiteral("completed_count"),
        QStringLiteral("non_completed_count"),
        QStringLiteral("usage_reported_count"),
        QStringLiteral("input_tokens"),
        QStringLiteral("output_tokens"),
        QStringLiteral("total_tokens"),
        QStringLiteral("reasoning_tokens"),
        QStringLiteral("cached_input_tokens"),
        QStringLiteral("duration_ms_total"),
    };
    return columns;
}

QString csv_escape(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) == true || escaped.contains(QLatin1Char('"')) == true ||
        escaped.contains(QLatin1Char('\n')) == true || escaped.contains(QLatin1Char('\r')) == true)
    {
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

QString csv_join(const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString &field : fields)
    {
        escaped.append(csv_escape(field));
    }
    return escaped.join(QLatin1Char(','));
}

bool parse_csv_line(const QString &line, QStringList *fields, QString *error)
{
    if (fields == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("CSV fields target is null");
        }
        return false;
    }

    QStringList parsed_fields;
    QString current_field;
    bool in_quotes = false;
    for (int index = 0; index < line.size(); ++index)
    {
        const QChar ch = line.at(index);
        if (in_quotes == true)
        {
            if (ch == QLatin1Char('"'))
            {
                if (index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"'))
                {
                    current_field += QLatin1Char('"');
                    ++index;
                }
                else
                {
                    in_quotes = false;
                }
            }
            else
            {
                current_field += ch;
            }
            continue;
        }

        if (ch == QLatin1Char(','))
        {
            parsed_fields.append(current_field);
            current_field.clear();
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            if (current_field.isEmpty() == false)
            {
                if (error != nullptr)
                {
                    *error = QStringLiteral("Malformed CSV line: quote appears mid-field");
                }
                return false;
            }
            in_quotes = true;
            continue;
        }

        current_field += ch;
    }

    if (in_quotes == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Malformed CSV line: unterminated quoted field");
        }
        return false;
    }

    parsed_fields.append(current_field);
    *fields = parsed_fields;
    return true;
}

bool parse_qint64_field(const QString &value, qint64 *target, const QString &field_name,
                        QString *error)
{
    if (target == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Target is null for field %1").arg(field_name);
        }
        return false;
    }

    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (ok == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Invalid integer value '%1' for field %2").arg(value, field_name);
        }
        return false;
    }
    *target = parsed;
    return true;
}

bool usage_stats_row_from_fields(const QStringList &fields, usage_stats_row_t *row, QString *error)
{
    if (row == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Usage stats row target is null");
        }
        return false;
    }

    if (fields.size() != usage_stats_header_columns().size())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Unexpected stats CSV column count: %1").arg(fields.size());
        }
        return false;
    }

    usage_stats_row_t parsed_row;
    parsed_row.date_local = fields.at(0);
    parsed_row.hour_local = fields.at(1);
    parsed_row.tz = fields.at(2);
    parsed_row.provider = fields.at(3);
    parsed_row.model = fields.at(4);
    parsed_row.request_kind = fields.at(5);
    if (fields.at(6) == QStringLiteral("true"))
    {
        parsed_row.dry_run = true;
    }
    else if (fields.at(6) == QStringLiteral("false"))
    {
        parsed_row.dry_run = false;
    }
    else
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid boolean value '%1' for dry_run").arg(fields.at(6));
        }
        return false;
    }

    if (parse_qint64_field(fields.at(7), &parsed_row.request_count,
                           QStringLiteral("request_count"), error) == false ||
        parse_qint64_field(fields.at(8), &parsed_row.completed_count,
                           QStringLiteral("completed_count"), error) == false ||
        parse_qint64_field(fields.at(9), &parsed_row.non_completed_count,
                           QStringLiteral("non_completed_count"), error) == false ||
        parse_qint64_field(fields.at(10), &parsed_row.usage_reported_count,
                           QStringLiteral("usage_reported_count"), error) == false ||
        parse_qint64_field(fields.at(11), &parsed_row.input_tokens, QStringLiteral("input_tokens"),
                           error) == false ||
        parse_qint64_field(fields.at(12), &parsed_row.output_tokens,
                           QStringLiteral("output_tokens"), error) == false ||
        parse_qint64_field(fields.at(13), &parsed_row.total_tokens, QStringLiteral("total_tokens"),
                           error) == false ||
        parse_qint64_field(fields.at(14), &parsed_row.reasoning_tokens,
                           QStringLiteral("reasoning_tokens"), error) == false ||
        parse_qint64_field(fields.at(15), &parsed_row.cached_input_tokens,
                           QStringLiteral("cached_input_tokens"), error) == false ||
        parse_qint64_field(fields.at(16), &parsed_row.duration_ms_total,
                           QStringLiteral("duration_ms_total"), error) == false)
    {
        return false;
    }

    *row = parsed_row;
    return true;
}

QStringList usage_stats_row_fields(const usage_stats_row_t &row)
{
    return {
        row.date_local,
        row.hour_local,
        row.tz,
        row.provider,
        row.model,
        row.request_kind,
        row.dry_run == true ? QStringLiteral("true") : QStringLiteral("false"),
        QString::number(row.request_count),
        QString::number(row.completed_count),
        QString::number(row.non_completed_count),
        QString::number(row.usage_reported_count),
        QString::number(row.input_tokens),
        QString::number(row.output_tokens),
        QString::number(row.total_tokens),
        QString::number(row.reasoning_tokens),
        QString::number(row.cached_input_tokens),
        QString::number(row.duration_ms_total),
    };
}

QDateTime parse_iso_datetime_utc(const QString &value)
{
    QDateTime timestamp = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (timestamp.isValid() == false)
    {
        timestamp = QDateTime::fromString(value, Qt::ISODate);
    }
    return timestamp.toUTC();
}

qint64 usage_value_or_zero(int value)
{
    return value >= 0 ? static_cast<qint64>(value) : 0;
}

qint64 run_duration_ms(const run_record_t &run)
{
    const QDateTime started_at = parse_iso_datetime_utc(run.started_at);
    const QDateTime finished_at = parse_iso_datetime_utc(run.finished_at);
    if (started_at.isValid() == false || finished_at.isValid() == false)
    {
        return 0;
    }
    const qint64 duration = started_at.msecsTo(finished_at);
    return duration >= 0 ? duration : 0;
}

QString utc_offset_string(const QDateTime &date_time)
{
    const int offset_seconds = date_time.offsetFromUtc();
    const QChar sign = offset_seconds >= 0 ? QLatin1Char('+') : QLatin1Char('-');
    const int absolute_seconds = std::abs(offset_seconds);
    const int hours = absolute_seconds / 3600;
    const int minutes = (absolute_seconds % 3600) / 60;
    return QStringLiteral("%1%2:%3")
        .arg(sign)
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'));
}

QString usage_stats_directory_path(const QString &workspace_root)
{
    return qcai2_path_for_workspace(workspace_root, QStringLiteral("stats"));
}

QString usage_stats_file_path(const QString &workspace_root, const QDate &date_local)
{
    return QDir(usage_stats_directory_path(workspace_root))
        .filePath(QStringLiteral("usage-%1.csv").arg(date_local.toString(Qt::ISODate)));
}

bool load_usage_stats_rows(const QString &path, QList<usage_stats_row_t> *rows, QString *error)
{
    if (rows == nullptr)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Usage stats rows target is null");
        }
        return false;
    }

    rows->clear();
    QFile file(path);
    if (file.exists() == false)
    {
        return true;
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to open %1 for reading: %2").arg(path, file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    if (stream.atEnd() == true)
    {
        return true;
    }

    QString header_line = stream.readLine();
    if (header_line.endsWith(QLatin1Char('\r')) == true)
    {
        header_line.chop(1);
    }
    QStringList header_fields;
    if (parse_csv_line(header_line, &header_fields, error) == false)
    {
        return false;
    }
    if (header_fields != usage_stats_header_columns())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Unexpected stats CSV header in %1").arg(path);
        }
        return false;
    }

    while (stream.atEnd() == false)
    {
        QString line = stream.readLine();
        if (line.endsWith(QLatin1Char('\r')) == true)
        {
            line.chop(1);
        }
        if (line.trimmed().isEmpty() == true)
        {
            continue;
        }

        QStringList fields;
        if (parse_csv_line(line, &fields, error) == false)
        {
            return false;
        }

        usage_stats_row_t row;
        if (usage_stats_row_from_fields(fields, &row, error) == false)
        {
            return false;
        }
        rows->append(row);
    }

    return true;
}

bool write_usage_stats_rows(const QString &path, const QList<usage_stats_row_t> &rows,
                            QString *error)
{
    const QFileInfo file_info(path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to create stats directory for %1").arg(path);
        }
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Failed to open %1 for writing: %2").arg(path, file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    stream << csv_join(usage_stats_header_columns()) << Qt::endl;
    for (const usage_stats_row_t &row : rows)
    {
        stream << csv_join(usage_stats_row_fields(row)) << Qt::endl;
    }

    if (file.commit() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to commit usage stats file %1").arg(path);
        }
        return false;
    }

    return true;
}

void accumulate_usage_stats_row(usage_stats_row_t *row, const run_record_t &run)
{
    row->request_count += 1;
    if (run.status == QStringLiteral("completed"))
    {
        row->completed_count += 1;
    }
    else
    {
        row->non_completed_count += 1;
    }

    if (run.usage.has_any() == true)
    {
        row->usage_reported_count += 1;
    }

    row->input_tokens += usage_value_or_zero(run.usage.input_tokens);
    row->output_tokens += usage_value_or_zero(run.usage.output_tokens);
    row->total_tokens += usage_value_or_zero(run.usage.resolved_total_tokens());
    row->reasoning_tokens += usage_value_or_zero(run.usage.reasoning_tokens);
    row->cached_input_tokens += usage_value_or_zero(run.usage.cached_input_tokens);
    row->duration_ms_total += run_duration_ms(run);
}

bool record_usage_stats(const QString &workspace_root, const run_record_t &run, QString *error)
{
    if (workspace_root.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Cannot write usage stats without an active workspace root");
        }
        return false;
    }

    QDateTime finished_at_utc = parse_iso_datetime_utc(run.finished_at);
    if (finished_at_utc.isValid() == false)
    {
        finished_at_utc = QDateTime::currentDateTimeUtc();
    }
    const QDateTime finished_at_local = finished_at_utc.toLocalTime();

    usage_stats_row_t new_row;
    new_row.date_local = finished_at_local.date().toString(Qt::ISODate);
    new_row.hour_local =
        QStringLiteral("%1").arg(finished_at_local.time().hour(), 2, 10, QLatin1Char('0'));
    new_row.tz = utc_offset_string(finished_at_local);
    new_row.provider = known_or_unknown(run.provider_id);
    new_row.model = known_or_unknown(run.model);
    new_row.request_kind = known_or_unknown(run.kind);
    new_row.dry_run = run.dry_run;

    const QString path = usage_stats_file_path(workspace_root, finished_at_local.date());
    QList<usage_stats_row_t> rows;
    if (load_usage_stats_rows(path, &rows, error) == false)
    {
        return false;
    }

    const QString new_key = new_row.key();
    for (usage_stats_row_t &row : rows)
    {
        if (row.key() == new_key)
        {
            accumulate_usage_stats_row(&row, run);
            return write_usage_stats_rows(path, rows, error);
        }
    }

    accumulate_usage_stats_row(&new_row, run);
    rows.append(new_row);
    return write_usage_stats_rows(path, rows, error);
}

}  // namespace

chat_context_manager_t::chat_context_manager_t(QObject *parent) : QObject(parent)
{
    this->store = std::make_unique<chat_context_store_t>();
}

bool chat_context_manager_t::set_active_workspace(const QString &workspace_id,
                                                  const QString &workspace_root,
                                                  const QString &conversation_id, QString *error)
{
    this->workspace_id = workspace_id;
    this->workspace_root = workspace_root;
    this->conversation = {};
    this->conversation.workspace_id = workspace_id;
    this->conversation.workspace_root = workspace_root;
    this->conversation.conversation_id = conversation_id;
    this->active_runs.clear();

    if (this->ensure_store_open(error) == false)
    {
        return false;
    }

    if (conversation_id.isEmpty() == false)
    {
        const conversation_record_t storedConversation =
            this->store->conversation(conversation_id, error);
        if (storedConversation.is_valid() == true)
        {
            this->conversation = storedConversation;
        }
        else
        {
            this->conversation.workspace_id = workspace_id;
            this->conversation.workspace_root = workspace_root;
            this->conversation.conversation_id = conversation_id;
        }
    }

    emit this->active_workspace_changed(this->workspace_id, this->workspace_root,
                                        this->conversation.conversation_id);

    return true;
}

QString chat_context_manager_t::active_workspace_id() const
{
    return this->workspace_id;
}

QString chat_context_manager_t::active_workspace_root() const
{
    return this->workspace_root;
}

QString chat_context_manager_t::active_conversation_id() const
{
    return this->conversation.conversation_id;
}

conversation_record_t chat_context_manager_t::active_conversation() const
{
    return this->conversation;
}

QString chat_context_manager_t::ensure_active_conversation(QString *error)
{
    if (this->ensure_conversation_available(error) == false)
    {
        return {};
    }
    return this->conversation.conversation_id;
}

QString chat_context_manager_t::start_new_conversation(const QString &title, QString *error)
{
    if (this->workspace_id.isEmpty() == true || this->workspace_root.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Active workspace is not set");
        }
        return {};
    }

    this->conversation = {};
    this->conversation.conversation_id = new_context_id();
    this->conversation.workspace_id = this->workspace_id;
    this->conversation.workspace_root = this->workspace_root;
    this->conversation.title = title.isNull() == true ? QStringLiteral("") : title;
    this->conversation.created_at = now_utc_iso_string();
    this->conversation.updated_at = this->conversation.created_at;

    if (this->ensure_store_open(error) == false)
    {
        return {};
    }
    if (this->store->ensure_conversation(&this->conversation, error) == false)
    {
        return {};
    }
    return this->conversation.conversation_id;
}

QList<conversation_record_t> chat_context_manager_t::conversations(QString *error) const
{
    if (this->ensure_store_open(error) == false)
    {
        return {};
    }
    if (this->workspace_id.isEmpty() == true)
    {
        return {};
    }
    return this->store->conversations(this->workspace_id, error);
}

bool chat_context_manager_t::rename_conversation(const QString &conversation_id,
                                                 const QString &title, QString *error)
{
    if (this->ensure_store_open(error) == false)
    {
        return false;
    }
    if (conversation_id.trimmed().isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation identifier is empty");
        }
        return false;
    }

    conversation_record_t record = this->store->conversation(conversation_id.trimmed(), error);
    if (record.is_valid() == false)
    {
        if (error != nullptr && error->isEmpty() == true)
        {
            *error = QStringLiteral("Conversation not found: %1").arg(conversation_id);
        }
        return false;
    }

    record.title = title.trimmed();
    record.updated_at = now_utc_iso_string();
    if (this->store->ensure_conversation(&record, error) == false)
    {
        return false;
    }

    if (this->conversation.conversation_id == record.conversation_id)
    {
        this->conversation = record;
    }
    return true;
}

bool chat_context_manager_t::delete_conversation(const QString &conversation_id, QString *error)
{
    const QString trimmed_id = conversation_id.trimmed();
    if (this->ensure_store_open(error) == false)
    {
        return false;
    }
    if (trimmed_id.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Conversation identifier is empty");
        }
        return false;
    }
    if (this->store->delete_conversation(trimmed_id, error) == false)
    {
        return false;
    }

    for (auto it = this->active_runs.begin(); it != this->active_runs.end();)
    {
        if (it.value().conversation_id == trimmed_id)
        {
            it = this->active_runs.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (this->conversation.conversation_id == trimmed_id)
    {
        this->conversation = {};
        this->conversation.workspace_id = this->workspace_id;
        this->conversation.workspace_root = this->workspace_root;
    }
    return true;
}

void chat_context_manager_t::sync_workspace_state(const editor_context_t::snapshot_t &snapshot,
                                                  const settings_t &settings, QString *error)
{
    if (this->ensure_store_open(error) == false)
    {
        return;
    }
    if (this->workspace_id.isEmpty() == true)
    {
        return;
    }

    const QString codingStylePath =
        QDir(this->workspace_root).filePath(QStringLiteral("docs/CODING_STYLE.md"));
    const bool codingStyleExists = QFileInfo::exists(codingStylePath);

    QList<memory_item_t> items;
    items.append(make_memory_item(
        this->workspace_id, QStringLiteral("workspace_root"), QStringLiteral("workspace"),
        known_or_unknown(this->workspace_root), QStringLiteral("editor_context"), 100));
    items.append(make_memory_item(
        this->workspace_id, QStringLiteral("build_directory"), QStringLiteral("workspace"),
        known_or_unknown(snapshot.build_dir), QStringLiteral("editor_context"), 95));
    items.append(make_memory_item(
        this->workspace_id, QStringLiteral("active_project"), QStringLiteral("workspace"),
        known_or_unknown(snapshot.project_file_path), QStringLiteral("editor_context"), 95));
    if (snapshot.file_path.isEmpty() == false)
    {
        items.append(make_memory_item(this->workspace_id, QStringLiteral("active_file"),
                                      QStringLiteral("workspace"), snapshot.file_path,
                                      QStringLiteral("editor_context"), 80));
    }
    if (codingStyleExists == true)
    {
        items.append(make_memory_item(this->workspace_id, QStringLiteral("coding_style"),
                                      QStringLiteral("preference"),
                                      QStringLiteral("Follow %1").arg(codingStylePath),
                                      QStringLiteral("workspace_doc"), 90));
    }
    items.append(make_memory_item(
        this->workspace_id, QStringLiteral("safety_rules"), QStringLiteral("constraint"),
        QStringLiteral("Do not reveal secrets, avoid destructive commands "
                       "without approval, and surface errors explicitly."),
        QStringLiteral("built_in"), 85));
    items.append(make_memory_item(
        this->workspace_id, QStringLiteral("agent_defaults"), QStringLiteral("preference"),
        QStringLiteral("model=%1; reasoning=%2; thinking=%3; dry_run_default=%4")
            .arg(settings.model_name, settings.reasoning_effort, settings.thinking_level,
                 settings.dry_run_default == true ? QStringLiteral("true")
                                                  : QStringLiteral("false")),
        QStringLiteral("settings"), 70));
    const completion_request_settings_t completion_settings =
        resolve_completion_request_settings(settings);
    items.append(make_memory_item(
        this->workspace_id, QStringLiteral("completion_defaults"), QStringLiteral("preference"),
        QStringLiteral("provider=%1; model=%2; send_reasoning=%3 (%4); send_thinking=%5 (%6)")
            .arg(completion_settings.provider_id, completion_settings.model,
                 completion_settings.send_reasoning == true ? QStringLiteral("true")
                                                            : QStringLiteral("false"),
                 completion_settings.selected_reasoning_effort,
                 completion_settings.send_thinking == true ? QStringLiteral("true")
                                                           : QStringLiteral("false"),
                 completion_settings.selected_thinking_level),
        QStringLiteral("settings"), 60));

    for (memory_item_t &item : items)
    {
        if (this->store->upsert_memory_item(&item, error) == false)
        {
            return;
        }
    }
}

QString chat_context_manager_t::begin_run(context_request_kind_t kind, const QString &provider_id,
                                          const QString &model, const QString &reasoning_effort,
                                          const QString &thinking_level, bool dry_run,
                                          const QJsonObject &metadata, QString *error)
{
    if (this->ensure_conversation_available(error) == false)
    {
        return {};
    }

    run_record_t run;
    run.run_id = new_context_id();
    run.workspace_id = this->workspace_id;
    run.conversation_id = this->conversation.conversation_id;
    run.kind = request_kind_name(kind);
    run.provider_id = provider_id;
    run.model = model;
    run.reasoning_effort = reasoning_effort;
    run.thinking_level = thinking_level;
    run.dry_run = dry_run;
    run.status = QStringLiteral("running");
    run.started_at = now_utc_iso_string();
    run.finished_at = QStringLiteral("");
    run.metadata = metadata;

    if (this->store->save_run(run, error) == false)
    {
        return {};
    }

    this->active_runs.insert(run.run_id, run);
    return run.run_id;
}

bool chat_context_manager_t::finish_run(const QString &run_id, const QString &status,
                                        const provider_usage_t &usage, const QJsonObject &metadata,
                                        QString *error)
{
    if (this->ensure_store_open(error) == false)
    {
        return false;
    }

    run_record_t run = this->active_runs.value(run_id);
    if (run.run_id.isEmpty() == true)
    {
        run.run_id = run_id;
        run.status = status;
    }

    run.status = status;
    run.finished_at = now_utc_iso_string();
    run.usage = usage;
    run.metadata = merge_json_objects(run.metadata, metadata);

    const bool updated = this->store->update_run(run, error);
    if (updated == false && error != nullptr && error->isEmpty() == true)
    {
        *error = QStringLiteral("Run record was not updated");
    }

    bool stats_recorded = true;
    if (updated == true)
    {
        QString stats_error;
        stats_recorded = record_usage_stats(this->workspace_root, run, &stats_error);
        if (stats_recorded == false && error != nullptr)
        {
            *error = stats_error;
        }
    }
    this->active_runs.remove(run_id);
    return updated && stats_recorded;
}

bool chat_context_manager_t::append_user_message(const QString &run_id, const QString &content,
                                                 const QString &source,
                                                 const QJsonObject &metadata, QString *error)
{
    return this->append_message(run_id, QStringLiteral("user"), content, source, metadata, error);
}

bool chat_context_manager_t::append_assistant_message(const QString &run_id,
                                                      const QString &content,
                                                      const QString &source,
                                                      const QJsonObject &metadata, QString *error)
{
    return this->append_message(run_id, QStringLiteral("assistant"), content, source, metadata,
                                error);
}

bool chat_context_manager_t::append_compaction_summary(const QString &run_id,
                                                       const QString &content,
                                                       const QJsonObject &metadata, QString *error)
{
    const QString trimmed_content = content.trimmed();
    if (trimmed_content.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Compaction summary is empty");
        }
        return false;
    }
    if (this->ensure_conversation_available(error) == false)
    {
        return false;
    }

    QString localError;
    const context_summary_t latest =
        this->store->latest_summary(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }

    const int latest_sequence =
        this->store->latest_message_sequence(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }

    context_summary_t summary;
    summary.workspace_id = this->workspace_id;
    summary.conversation_id = this->conversation.conversation_id;
    summary.version = latest.version + 1;
    summary.start_sequence =
        latest.is_valid() == true ? latest.start_sequence : (latest_sequence > 0 ? 1 : 0);
    summary.end_sequence = qMax(latest_sequence, summary.start_sequence);
    summary.content = trimmed_content;
    summary.metadata = merge_json_objects(
        metadata, QJsonObject{{QStringLiteral("source"), QStringLiteral("compaction")},
                              {QStringLiteral("runId"), run_id},
                              {QStringLiteral("previousSummaryId"), latest.summary_id}});
    return this->store->add_summary(&summary, error);
}

bool chat_context_manager_t::append_artifact(const QString &run_id, const QString &kind,
                                             const QString &title, const QString &content,
                                             const QJsonObject &metadata, QString *error)
{
    if (this->ensure_conversation_available(error) == false)
    {
        return false;
    }

    artifact_record_t artifact;
    artifact.workspace_id = this->workspace_id;
    artifact.conversation_id = this->conversation.conversation_id;
    artifact.run_id = run_id;
    artifact.kind = kind;
    artifact.title = title;
    artifact.preview = truncate_for_context(content, 1000);
    artifact.metadata = metadata;

    return this->store->add_artifact(&artifact, content, error);
}

context_envelope_t chat_context_manager_t::build_context_envelope(
    context_request_kind_t kind, const QString &system_instruction,
    const QStringList &dynamic_system_messages, int max_output_tokens, QString *error) const
{
    context_envelope_t envelope;
    if (this->ensure_store_open(error) == false)
    {
        return envelope;
    }
    if (this->conversation.is_valid() == false)
    {
        return envelope;
    }

    envelope.workspace_id = this->workspace_id;
    envelope.conversation_id = this->conversation.conversation_id;
    envelope.budget = budget_for_request(kind, max_output_tokens);

    QString localError;
    envelope.memory_items = this->select_memory_items(envelope.budget, &localError);
    if (propagate_error(error, localError) == true)
    {
        return context_envelope_t{};
    }

    envelope.summary =
        this->store->latest_summary(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return context_envelope_t{};
    }

    envelope.artifacts = this->select_artifacts(envelope.budget, &localError);
    if (propagate_error(error, localError) == true)
    {
        return context_envelope_t{};
    }

    envelope.recent_messages = this->select_recent_messages(envelope.budget, &localError);
    if (propagate_error(error, localError) == true)
    {
        return context_envelope_t{};
    }

    if (system_instruction.isEmpty() == false)
    {
        envelope.provider_messages.append(
            chat_message_t{QStringLiteral("system"), system_instruction});
    }

    const QString memoryText =
        this->memory_block(envelope.memory_items, envelope.budget.memory_tokens);
    if (memoryText.isEmpty() == false)
    {
        envelope.provider_messages.append(chat_message_t{QStringLiteral("system"), memoryText});
    }

    const QString summaryText =
        this->conversation_summary_block(envelope.summary, envelope.budget.summary_tokens);
    if (summaryText.isEmpty() == false)
    {
        envelope.provider_messages.append(chat_message_t{QStringLiteral("system"), summaryText});
    }

    const QString artifactText =
        this->artifact_block(envelope.artifacts, envelope.budget.artifact_tokens);
    if (artifactText.isEmpty() == false)
    {
        envelope.provider_messages.append(chat_message_t{QStringLiteral("system"), artifactText});
    }

    for (const QString &message : dynamic_system_messages)
    {
        if (message.isEmpty() == false)
        {
            envelope.provider_messages.append(chat_message_t{QStringLiteral("system"), message});
        }
    }

    for (const context_message_t &message : envelope.recent_messages)
    {
        envelope.provider_messages.append(message.to_chat_message());
    }

    return envelope;
}

QString chat_context_manager_t::build_completion_context_block(const QString &file_path,
                                                               int max_output_tokens,
                                                               QString *error) const
{
    if (this->ensure_store_open(error) == false)
    {
        return {};
    }
    if (this->conversation.is_valid() == false)
    {
        return {};
    }

    const context_budget_t budget =
        budget_for_request(context_request_kind_t::COMPLETION, max_output_tokens);

    QString localError;
    const QList<memory_item_t> memory_items = this->select_memory_items(budget, &localError);
    if (propagate_error(error, localError) == true)
    {
        return {};
    }

    const context_summary_t summary =
        this->store->latest_summary(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return {};
    }

    const QList<artifact_record_t> artifacts = this->select_artifacts(budget, &localError);
    if (propagate_error(error, localError) == true)
    {
        return {};
    }

    const QList<context_message_t> messages = this->select_recent_messages(budget, &localError);
    if (propagate_error(error, localError) == true)
    {
        return {};
    }

    QStringList parts;
    parts.append(QStringLiteral("Completion context for %1").arg(file_path));

    const QString memoryText = this->memory_block(memory_items, budget.memory_tokens);
    if (memoryText.isEmpty() == false)
    {
        parts.append(memoryText);
    }

    const QString summaryText = this->conversation_summary_block(summary, budget.summary_tokens);
    if (summaryText.isEmpty() == false)
    {
        parts.append(summaryText);
    }

    const QString messageText =
        this->recent_messages_block(messages, budget.recent_message_tokens);
    if (messageText.isEmpty() == false)
    {
        parts.append(messageText);
    }

    const QString artifactText = this->artifact_block(artifacts, budget.artifact_tokens);
    if (artifactText.isEmpty() == false)
    {
        parts.append(artifactText);
    }

    return parts.join(QStringLiteral("\n\n"));
}

bool chat_context_manager_t::maybe_refresh_summary(QString *error)
{
    if (this->ensure_conversation_available(error) == false)
    {
        return false;
    }

    QString localError;
    const context_summary_t latest =
        this->store->latest_summary(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }

    const int latestSequence =
        this->store->latest_message_sequence(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }
    if (latestSequence <= latest.end_sequence)
    {
        return true;
    }

    const QList<context_message_t> unsummarizedMessages = this->store->messages_range(
        this->conversation.conversation_id, latest.end_sequence, latestSequence, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }

    int unsummarizedTokens = 0;
    for (const context_message_t &message : unsummarizedMessages)
    {
        unsummarizedTokens += message.token_estimate;
    }

    if ((unsummarizedMessages.size() < kSummaryRefreshMessageThreshold) &&
        (unsummarizedTokens < kSummaryRefreshTokenThreshold))
    {
        return true;
    }

    QList<context_message_t> chunk;
    int chunkTokens = 0;
    for (const context_message_t &message : unsummarizedMessages)
    {
        if ((chunk.size() >= kSummaryChunkMaxMessages) ||
            (((chunkTokens + message.token_estimate) > kSummaryChunkMaxTokens) &&
             (chunk.isEmpty() == false)))
        {
            break;
        }
        chunk.append(message);
        chunkTokens += message.token_estimate;
    }
    if (chunk.isEmpty() == true)
    {
        return true;
    }

    const QList<artifact_record_t> artifacts =
        this->store->recent_artifacts(this->conversation.conversation_id, 4, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }

    context_summary_t nextSummary =
        this->compose_next_summary(latest, chunk, artifacts, &localError);
    if (propagate_error(error, localError) == true)
    {
        return false;
    }
    if (nextSummary.is_valid() == false)
    {
        return true;
    }

    return this->store->add_summary(&nextSummary, error);
}

QString chat_context_manager_t::database_path() const
{
    return this->database_path_for_workspace();
}

QString chat_context_manager_t::artifact_directory_path() const
{
    return this->artifact_path_for_workspace();
}

bool chat_context_manager_t::ensure_store_open(QString *error) const
{
    if (this->workspace_root.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Workspace root is not set");
        }
        return false;
    }
    return this->store->open(this->database_path_for_workspace(),
                             this->artifact_path_for_workspace(), error);
}

bool chat_context_manager_t::ensure_conversation_available(QString *error)
{
    if (this->ensure_store_open(error) == false)
    {
        return false;
    }
    if (this->conversation.is_valid() == false)
    {
        return this->start_new_conversation({}, error).isEmpty() == false;
    }
    return this->store->ensure_conversation(&this->conversation, error);
}

bool chat_context_manager_t::append_message(const QString &run_id, const QString &role,
                                            const QString &content, const QString &source,
                                            const QJsonObject &metadata, QString *error)
{
    if (content.isEmpty() == true)
    {
        return true;
    }
    if (this->ensure_conversation_available(error) == false)
    {
        return false;
    }

    context_message_t message;
    message.workspace_id = this->workspace_id;
    message.conversation_id = this->conversation.conversation_id;
    message.run_id = run_id;
    message.role = role;
    message.source = source;
    message.content = content;
    message.metadata = metadata;
    if (this->store->append_message(&message, error) == false)
    {
        return false;
    }

    this->conversation.last_message_sequence = message.sequence;
    this->conversation.updated_at = message.created_at;
    emit this->message_appended(this->workspace_id, this->workspace_root,
                                this->conversation.conversation_id, message.message_id,
                                message.role, message.source, message.content, message.created_at);
    return this->maybe_refresh_summary(error);
}

QString chat_context_manager_t::conversation_summary_block(const context_summary_t &summary,
                                                           int max_tokens) const
{
    if (summary.is_valid() == false || max_tokens <= 0)
    {
        return {};
    }

    return QStringLiteral("Rolling summary (version %1, messages %2-%3):\n%4")
        .arg(summary.version)
        .arg(summary.start_sequence)
        .arg(summary.end_sequence)
        .arg(truncate_for_context(summary.content, max_tokens * 4));
}

QString chat_context_manager_t::memory_block(const QList<memory_item_t> &items,
                                             int max_tokens) const
{
    if (items.isEmpty() == true || max_tokens <= 0)
    {
        return {};
    }

    QStringList lines;
    lines.append(QStringLiteral("Stable workspace memory:"));
    int usedTokens = estimate_token_count(lines.constFirst());
    for (const memory_item_t &item : items)
    {
        const QString line =
            QStringLiteral("- %1 (%2): %3")
                .arg(item.key, item.category, truncate_for_context(item.value, 220));
        const int itemTokens = estimate_token_count(line);
        if (((usedTokens + itemTokens) > max_tokens) == true)
        {
            break;
        }
        lines.append(line);
        usedTokens += itemTokens;
    }
    return lines.join(QStringLiteral("\n"));
}

QString chat_context_manager_t::artifact_block(const QList<artifact_record_t> &artifacts,
                                               int max_tokens) const
{
    if (artifacts.isEmpty() == true || max_tokens <= 0)
    {
        return {};
    }

    QStringList lines;
    lines.append(QStringLiteral("Relevant prior artifacts (referenced, not inlined):"));
    int usedTokens = estimate_token_count(lines.constFirst());
    for (const artifact_record_t &artifact : artifacts)
    {
        const QString line = this->render_artifact_bullet(artifact, 280);
        const int itemTokens = estimate_token_count(line);
        if (((usedTokens + itemTokens) > max_tokens) == true)
        {
            break;
        }
        lines.append(line);
        usedTokens += itemTokens;
    }
    return lines.join(QStringLiteral("\n"));
}

QString chat_context_manager_t::recent_messages_block(const QList<context_message_t> &messages,
                                                      int max_tokens) const
{
    if (messages.isEmpty() == true || max_tokens <= 0)
    {
        return {};
    }

    QStringList lines;
    lines.append(QStringLiteral("Recent conversation excerpts:"));
    int usedTokens = estimate_token_count(lines.constFirst());
    for (const context_message_t &message : messages)
    {
        const QString line = this->render_message_bullet(message, 180);
        const int itemTokens = estimate_token_count(line);
        if (((usedTokens + itemTokens) > max_tokens) == true)
        {
            break;
        }
        lines.append(line);
        usedTokens += itemTokens;
    }
    return lines.join(QStringLiteral("\n"));
}

QString chat_context_manager_t::render_message_bullet(const context_message_t &message,
                                                      int max_chars) const
{
    return QStringLiteral("- [%1/%2] %3")
        .arg(message.role, message.source,
             truncate_for_context(message.content.simplified(), max_chars));
}

QString chat_context_manager_t::render_artifact_bullet(const artifact_record_t &artifact,
                                                       int max_chars) const
{
    QString location = artifact.storage_path;
    if (location.isEmpty() == true)
    {
        location = QStringLiteral("preview-only");
    }
    return QStringLiteral("- [%1] %2 (%3): %4")
        .arg(artifact.kind, artifact.title, location,
             truncate_for_context(artifact.preview.simplified(), max_chars));
}

QList<memory_item_t> chat_context_manager_t::select_memory_items(const context_budget_t &budget,
                                                                 QString *error) const
{
    QList<memory_item_t> selected;
    const QList<memory_item_t> items = this->store->memory_items(this->workspace_id, 16, error);
    int usedTokens = 0;
    for (const memory_item_t &item : items)
    {
        if (((usedTokens + item.token_estimate) > budget.memory_tokens) == true)
        {
            break;
        }
        selected.append(item);
        usedTokens += item.token_estimate;
    }
    return selected;
}

QList<artifact_record_t> chat_context_manager_t::select_artifacts(const context_budget_t &budget,
                                                                  QString *error) const
{
    QList<artifact_record_t> selected;
    const QList<artifact_record_t> artifacts = this->store->recent_artifacts(
        this->conversation.conversation_id, budget.max_artifacts * 3, error);
    int usedTokens = 0;
    for (const artifact_record_t &artifact : artifacts)
    {
        if (selected.size() >= budget.max_artifacts)
        {
            break;
        }
        if (((usedTokens + artifact.token_estimate) > budget.artifact_tokens) == true)
        {
            continue;
        }
        selected.append(artifact);
        usedTokens += artifact.token_estimate;
    }
    return selected;
}

QList<context_message_t>
chat_context_manager_t::select_recent_messages(const context_budget_t &budget,
                                               QString *error) const
{
    QList<context_message_t> selected;
    QString localError;
    const context_summary_t latest_summary =
        this->store->latest_summary(this->conversation.conversation_id, &localError);
    if (propagate_error(error, localError) == true)
    {
        return {};
    }
    const QList<context_message_t> messages = this->store->recent_messages(
        this->conversation.conversation_id, budget.max_recent_messages * 2, error);
    int usedTokens = 0;
    for (qsizetype index = messages.size() - 1; index >= 0; --index)
    {
        const context_message_t &message = messages.at(static_cast<int>(index));
        if (latest_summary.is_valid() == true && message.sequence <= latest_summary.end_sequence)
        {
            continue;
        }
        if (message.include_in_prompt() == false)
        {
            continue;
        }
        if (((usedTokens + message.token_estimate) > budget.recent_message_tokens) == true &&
            (selected.isEmpty() == false))
        {
            continue;
        }
        selected.prepend(message);
        usedTokens += message.token_estimate;
        if (selected.size() >= budget.max_recent_messages)
        {
            break;
        }
    }
    return selected;
}

context_summary_t chat_context_manager_t::compose_next_summary(
    const context_summary_t &previous, const QList<context_message_t> &chunk,
    const QList<artifact_record_t> &artifacts, QString * /*error*/) const
{
    context_summary_t summary;
    if (chunk.isEmpty() == true)
    {
        return summary;
    }

    QStringList lines;
    lines.append(QStringLiteral("Rolling summary for workspace %1").arg(this->workspace_id));
    if (previous.is_valid() == true)
    {
        lines.append(QStringLiteral("Carry-forward summary:"));
        lines.append(truncate_for_context(previous.content, kSummaryCarryForwardChars));
    }
    lines.append(QStringLiteral("Newly summarized conversation:"));
    for (const context_message_t &message : chunk)
    {
        lines.append(this->render_message_bullet(message, 260));
    }
    if (artifacts.isEmpty() == false)
    {
        lines.append(QStringLiteral("Referenced artifacts:"));
        for (const artifact_record_t &artifact : artifacts)
        {
            lines.append(this->render_artifact_bullet(artifact, 200));
        }
    }

    summary.workspace_id = this->workspace_id;
    summary.conversation_id = this->conversation.conversation_id;
    summary.summary_id = new_context_id();
    summary.version = previous.version + 1;
    summary.start_sequence =
        previous.is_valid() == true ? previous.start_sequence : chunk.constFirst().sequence;
    summary.end_sequence = chunk.constLast().sequence;
    summary.created_at = now_utc_iso_string();
    summary.content = truncate_for_context(lines.join(QStringLiteral("\n")), kSummaryMaxChars);
    summary.metadata = QJsonObject{{QStringLiteral("previousSummaryId"), previous.summary_id},
                                   {QStringLiteral("chunkMessageCount"), chunk.size()},
                                   {QStringLiteral("artifactCount"), artifacts.size()}};
    return summary;
}

QString chat_context_manager_t::database_path_for_workspace() const
{
    return qcai2_path_for_workspace(this->workspace_root, QStringLiteral("chat-context"));
}

QString chat_context_manager_t::artifact_path_for_workspace() const
{
    return qcai2_path_for_workspace(this->workspace_root, QStringLiteral("artifacts"));
}

}  // namespace qcai2
