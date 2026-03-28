/*! @file
    @brief Implements debounced inline ghost-text completion requests.
*/

#include "ghost_text_manager.h"
#include "../clangd/clangd_service.h"
#include "../context/chat_context_manager.h"
#include "../models/agent_messages.h"
#include "../providers/iai_provider.h"
#include "../settings/settings.h"
#include "../util/completion_detailed_log.h"
#include "../util/logger.h"
#include "../util/project_root_resolver.h"
#include "../util/prompt_instructions.h"
#include "../util/system_diagnostics_log.h"
#include "completion_clang_context.h"
#include "completion_connection_utils.h"
#include "completion_request_settings.h"
#include "completion_response_utils.h"
#include "completion_trigger_utils.h"
#include "qompi_provider_backend_adapter.h"

#include <qompi/completion_session.h>
#include <qompi/completion_sink.h>
#include <qompi/completion_types.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textsuggestion.h>
#include <utils/textutils.h>

#include <QMetaObject>
#include <QTextCursor>
#include <QTextDocument>

#include <atomic>
#include <functional>

namespace qcai2
{

static const int kContextBefore = 2000;
static const int kContextAfter = 500;
namespace
{

std::atomic<int> s_next_completion_request_id{1};

std::string to_std_string(const QString &value)
{
    return value.toUtf8().toStdString();
}

QString from_std_string(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

bool runtime_completion_diagnostics_enabled()
{
    const auto &s = settings();
    return s.detailed_completion_logging == true || s.debug_logging == true;
}

QString
diagnostics_workspace_root_for_file_path(const QString &file_path,
                                         const chat_context_manager_t *chat_context_manager)
{
    if (chat_context_manager != nullptr)
    {
        const QString active_workspace_root = chat_context_manager->active_workspace_root();
        if (active_workspace_root.isEmpty() == false)
        {
            return active_workspace_root;
        }
    }

    if (file_path.isEmpty() == true)
    {
        return {};
    }

    const QString project_root = project_root_for_file_path(file_path);
    if (project_root.isEmpty() == false)
    {
        return project_root;
    }

    return {};
}

QString diagnostics_text_excerpt(QString text)
{
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    constexpr int diagnostics_excerpt_chars_max = QCAI2_GHOST_TEXT_DIAGNOSTICS_EXCERPT_CHARS_MAX;
    if (text.size() > diagnostics_excerpt_chars_max)
    {
        text = text.left(diagnostics_excerpt_chars_max) + QStringLiteral("...");
    }
    return text;
}

void append_runtime_completion_event(const chat_context_manager_t *chat_context_manager,
                                     const QString &component, const QString &event,
                                     const QString &message, const QStringList &details,
                                     const QString &file_path, const bool always_log = false)
{
    if (always_log == false && runtime_completion_diagnostics_enabled() == false)
    {
        return;
    }

    const QString workspace_root =
        diagnostics_workspace_root_for_file_path(file_path, chat_context_manager);
    if (workspace_root.isEmpty() == true)
    {
        return;
    }

    append_system_diagnostics_event(workspace_root, component, event, message, details);
}

iai_provider_t *find_provider_by_id(const QList<iai_provider_t *> &providers, const QString &id)
{
    for (iai_provider_t *provider : providers)
    {
        if (provider != nullptr && provider->id() == id)
        {
            return provider;
        }
    }
    return nullptr;
}

QList<completion_log_prompt_part_t> completion_prompt_parts(const QList<chat_message_t> &messages)
{
    QList<completion_log_prompt_part_t> parts;
    parts.reserve(messages.size());
    for (const chat_message_t &message : messages)
    {
        parts.append({message.role, message.content, estimate_token_count(message.content)});
    }
    return parts;
}

void finalize_detailed_completion_log(
    const std::shared_ptr<completion_detailed_log_t> &detailed_log, const QString &status,
    const QString &response, const QString &error, const provider_usage_t &usage)
{
    if (detailed_log == nullptr)
    {
        return;
    }

    detailed_log->finish_request(status, response, error, usage);
    QString log_error;
    if (detailed_log->append_to_project_log(&log_error) == false)
    {
        append_system_diagnostics_event(
            detailed_log->workspace_root_path(), QStringLiteral("GhostText"),
            QStringLiteral("request.log_write_failed"),
            QStringLiteral("Ghost-text detailed completion log write failed."),
            {QStringLiteral("status=%1").arg(status), QStringLiteral("error=%1").arg(log_error)});
        QCAI_WARN("GhostText",
                  QStringLiteral("Failed to append detailed completion log: %1").arg(log_error));
    }
    else
    {
        append_system_diagnostics_event(
            detailed_log->workspace_root_path(), QStringLiteral("GhostText"),
            QStringLiteral("request.log_write_succeeded"),
            QStringLiteral("Ghost-text detailed completion log write succeeded."),
            {QStringLiteral("status=%1").arg(status),
             QStringLiteral("target_path=%1").arg(detailed_log->log_file_path())});
    }
}

void record_streaming_stage(const std::shared_ptr<completion_detailed_log_t> &detailed_log,
                            const QString &stage, const QString &message)
{
    if (detailed_log == nullptr)
    {
        return;
    }
    detailed_log->record_stage(QStringLiteral("local"), stage, message);
}

class ghost_text_qompi_sink_t final : public qompi::completion_sink_t
{
public:
    ghost_text_qompi_sink_t(ghost_text_manager_t *owner,
                            QPointer<TextEditor::TextEditorWidget> editor, int pos, QString prefix,
                            QString suffix, std::shared_ptr<bool> alive,
                            std::shared_ptr<completion_detailed_log_t> detailed_log)
        : owner(owner), editor(std::move(editor)), pos(pos), prefix(std::move(prefix)),
          suffix(std::move(suffix)), alive(std::move(alive)), detailed_log(std::move(detailed_log))
    {
    }

    void on_chunk(const qompi::completion_chunk_t &chunk) override
    {
        if (*this->alive == false || this->editor.isNull() == true)
        {
            return;
        }

        provider_usage_t usage;
        if (this->owner->show_completion_suggestion(
                this->editor.data(), this->pos, this->prefix, this->suffix,
                from_std_string(chunk.stable_text), usage, this->detailed_log, false) == false)
        {
            return;
        }
        record_streaming_stage(
            this->detailed_log, QStringLiteral("streaming.partial_ready"),
            QStringLiteral("rendered_chars=%1").arg(from_std_string(chunk.stable_text).length()));
    }

    void on_debug_event(const std::string &stage, const std::string &message) override
    {
        if (this->detailed_log == nullptr)
        {
            return;
        }
        this->detailed_log->record_stage(QStringLiteral("qompi"), from_std_string(stage),
                                         from_std_string(message));
    }

    void on_completed(const qompi::completion_result_t &result) override
    {
        if (*this->alive == false || this->editor.isNull() == true)
        {
            finalize_detailed_completion_log(this->detailed_log, QStringLiteral("abandoned"),
                                             from_std_string(result.text),
                                             QStringLiteral("Editor destroyed before completion "
                                                            "callback."),
                                             {});
            return;
        }

        provider_usage_t usage;
        this->owner->handle_completion_response(this->editor.data(), this->pos, this->prefix,
                                                this->suffix, from_std_string(result.text),
                                                QString(), usage, this->detailed_log);
    }

    void on_failed(const qompi::completion_error_t &error) override
    {
        if (*this->alive == false || this->editor.isNull() == true)
        {
            return;
        }

        provider_usage_t usage;
        this->owner->handle_completion_response(
            this->editor.data(), this->pos, this->prefix, this->suffix, QString(),
            from_std_string(error.message), usage, this->detailed_log);
    }

private:
    ghost_text_manager_t *owner = nullptr;
    QPointer<TextEditor::TextEditorWidget> editor;
    int pos = 0;
    QString prefix;
    QString suffix;
    std::shared_ptr<bool> alive;
    std::shared_ptr<completion_detailed_log_t> detailed_log;
};

}  // namespace

ghost_text_manager_t::ghost_text_manager_t(QObject *parent) : QObject(parent)
{
}

ghost_text_manager_t::~ghost_text_manager_t()
{
    *this->alive = false;
}

iai_provider_t *ghost_text_manager_t::resolve_provider() const
{
    const completion_request_settings_t completion_settings =
        resolve_completion_request_settings(settings());
    if (iai_provider_t *resolved =
            find_provider_by_id(this->providers, completion_settings.provider_id);
        resolved != nullptr)
    {
        apply_completion_connection_settings(resolved,
                                             resolve_completion_connection_settings(settings()));
        return resolved;
    }
    if (this->provider != nullptr)
    {
        apply_completion_connection_settings(this->provider,
                                             resolve_completion_connection_settings(settings()));
        return this->provider;
    }
    if (this->providers.isEmpty() == false)
    {
        apply_completion_connection_settings(this->providers.first(),
                                             resolve_completion_connection_settings(settings()));
        return this->providers.first();
    }
    return nullptr;
}

void ghost_text_manager_t::set_enabled(bool enabled)
{
    this->enabled = enabled;
}

void ghost_text_manager_t::attach_to_editor(TextEditor::TextEditorWidget *editor)
{
    if ((((editor == nullptr) || this->debounce_timers.contains(editor)) == true))
    {
        return;
    }

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, editor]() { this->request_completion(editor); });
    this->debounce_timers.insert(editor, timer);
    this->last_edit_positions.insert(editor, editor->textCursor().position());

    const QString file_path = editor->textDocument() != nullptr
                                  ? editor->textDocument()->filePath().toUrlishString()
                                  : QString();
    append_runtime_completion_event(
        this->chat_context_manager, QStringLiteral("GhostText"), QStringLiteral("editor.attached"),
        QStringLiteral("Attached ghost-text monitoring to editor."),
        {QStringLiteral("file=%1").arg(
             file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
         QStringLiteral("cursor_position=%1").arg(editor->textCursor().position())},
        file_path, true);

    connect(
        editor->document(), &QTextDocument::contentsChange, this,
        [this, editor](int position, int /*charsRemoved*/, int charsAdded) {
            const QString file_path = editor->textDocument() != nullptr
                                          ? editor->textDocument()->filePath().toUrlishString()
                                          : QString();
            if (charsAdded <= 0)
            {
                append_runtime_completion_event(
                    this->chat_context_manager, QStringLiteral("GhostText"),
                    QStringLiteral("contents_change_ignored"),
                    QStringLiteral("Ignored contents change without inserted characters."),
                    {QStringLiteral("file=%1").arg(
                         file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
                     QStringLiteral("position=%1").arg(position),
                     QStringLiteral("chars_added=%1").arg(charsAdded)},
                    file_path);
                return;
            }
            const QString inserted_text =
                editor->document()->toPlainText().mid(position, charsAdded);
            if (is_non_whitespace_typed_insertion(inserted_text) == false)
            {
                append_runtime_completion_event(
                    this->chat_context_manager, QStringLiteral("GhostText"),
                    QStringLiteral("contents_change_ignored"),
                    QStringLiteral("Ignored contents change with whitespace-only insertion."),
                    {QStringLiteral("file=%1").arg(
                         file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
                     QStringLiteral("position=%1").arg(position),
                     QStringLiteral("chars_added=%1").arg(charsAdded),
                     QStringLiteral("inserted_text=%1")
                         .arg(diagnostics_text_excerpt(inserted_text))},
                    file_path);
                return;
            }
            append_runtime_completion_event(
                this->chat_context_manager, QStringLiteral("GhostText"),
                QStringLiteral("contents_change_seen"),
                QStringLiteral("Observed typed insertion for ghost-text completion."),
                {QStringLiteral("file=%1").arg(
                     file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
                 QStringLiteral("position=%1").arg(position),
                 QStringLiteral("chars_added=%1").arg(charsAdded),
                 QStringLiteral("cursor_position=%1").arg(position + charsAdded),
                 QStringLiteral("inserted_text=%1").arg(diagnostics_text_excerpt(inserted_text))},
                file_path);

            const QTextDocument *doc = editor->document();
            if (doc == nullptr)
            {
                return;
            }

            const int effective_fragment_end_offset = typed_fragment_end_offset(inserted_text);
            if (effective_fragment_end_offset <= 0)
            {
                return;
            }

            const int effective_cursor_position = position + effective_fragment_end_offset;
            this->on_contents_changed(editor, effective_cursor_position);
        });

    connect(editor, &QObject::destroyed, this, [this, editor]() {
        if (auto *t = this->debounce_timers.take(editor); ((t != nullptr) == true))
        {
            t->deleteLater();
        }
        this->last_edit_positions.remove(editor);
        this->pending_snapshots.remove(editor);
        this->qompi_sessions.remove(editor);
        this->qompi_session_provider_ids.remove(editor);
    });

    QCAI_DEBUG("GhostText", QStringLiteral("Attached to editor"));
}

void ghost_text_manager_t::on_contents_changed(TextEditor::TextEditorWidget *editor,
                                               const int cursor_position)
{
    const QString file_path = editor != nullptr && editor->textDocument() != nullptr
                                  ? editor->textDocument()->filePath().toUrlishString()
                                  : QString();
    if (((!this->enabled || (this->resolve_provider() == nullptr)) == true))
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("trigger_skipped"),
            QStringLiteral("Skipped ghost-text trigger because the manager is disabled or no "
                           "provider is available."),
            {QStringLiteral("file=%1").arg(
                 file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
             QStringLiteral("cursor_position=%1").arg(cursor_position),
             QStringLiteral("enabled=%1")
                 .arg(this->enabled == true ? QStringLiteral("true") : QStringLiteral("false")),
             QStringLiteral("provider_available=%1")
                 .arg(this->resolve_provider() != nullptr ? QStringLiteral("true")
                                                          : QStringLiteral("false"))},
            file_path);
        return;
    }

    const auto &s = settings();
    if (s.ai_completion_enabled == false)
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("trigger_skipped"),
            QStringLiteral(
                "Skipped ghost-text trigger because completion is disabled in settings."),
            {QStringLiteral("file=%1").arg(
                 file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
             QStringLiteral("cursor_position=%1").arg(cursor_position)},
            file_path);
        return;
    }

    const QTextDocument *doc = editor->document();
    if (doc == nullptr)
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("trigger_skipped"),
            QStringLiteral("Skipped ghost-text trigger because the editor has no document."),
            {QStringLiteral("cursor_position=%1").arg(cursor_position)}, file_path);
        return;
    }

    const QTextBlock trigger_block = doc->findBlock(qMax(0, cursor_position - 1));
    const int trigger_column = cursor_position - trigger_block.position();
    if (should_auto_trigger_completion(trigger_block.text(), trigger_column,
                                       s.completion_min_chars) == false)
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("trigger_skipped"),
            QStringLiteral("Skipped ghost-text trigger because auto-trigger rules rejected the "
                           "current edit."),
            {QStringLiteral("file=%1").arg(
                 file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
             QStringLiteral("cursor_position=%1").arg(cursor_position),
             QStringLiteral("trigger_column=%1").arg(trigger_column),
             QStringLiteral("completion_min_chars=%1").arg(s.completion_min_chars),
             QStringLiteral("line_text=%1").arg(diagnostics_text_excerpt(trigger_block.text()))},
            file_path);
        return;
    }

    const QString full_text = doc->toPlainText();
    const int start_before = qMax(0, cursor_position - kContextBefore);
    pending_completion_snapshot_t snapshot;
    snapshot.cursor_position = cursor_position;
    snapshot.prefix = full_text.mid(start_before, cursor_position - start_before);
    snapshot.suffix =
        full_text.mid(cursor_position, qMin(kContextAfter, full_text.length() - cursor_position));
    this->pending_snapshots.insert(editor, snapshot);
    this->last_edit_positions.insert(editor, cursor_position);
    if (auto *timer = this->debounce_timers.value(editor); ((timer != nullptr) == true))
    {
        timer->start(qMax(0, s.completion_delay_ms));
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("debounce_started"),
            QStringLiteral("Started ghost-text debounce after a valid typing trigger."),
            {QStringLiteral("file=%1").arg(
                 file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
             QStringLiteral("cursor_position=%1").arg(cursor_position),
             QStringLiteral("delay_ms=%1").arg(qMax(0, s.completion_delay_ms)),
             QStringLiteral("prefix_chars=%1").arg(snapshot.prefix.size()),
             QStringLiteral("suffix_chars=%1").arg(snapshot.suffix.size())},
            file_path);
    }
}

void ghost_text_manager_t::request_completion(TextEditor::TextEditorWidget *editor)
{
    const QString file_name = editor != nullptr && editor->textDocument() != nullptr
                                  ? editor->textDocument()->filePath().toUrlishString()
                                  : QString();
    iai_provider_t *provider = this->resolve_provider();
    if (((!this->enabled || (provider == nullptr) || (editor == nullptr)) == true))
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("request_skipped"),
            QStringLiteral("Skipped ghost-text request before dispatch."),
            {QStringLiteral("file=%1").arg(
                 file_name.isEmpty() == false ? file_name : QStringLiteral("<untitled>")),
             QStringLiteral("enabled=%1")
                 .arg(this->enabled == true ? QStringLiteral("true") : QStringLiteral("false")),
             QStringLiteral("provider_available=%1")
                 .arg(provider != nullptr ? QStringLiteral("true") : QStringLiteral("false")),
             QStringLiteral("editor_available=%1")
                 .arg(editor != nullptr ? QStringLiteral("true") : QStringLiteral("false"))},
            file_name);
        return;
    }

    const auto &s = settings();
    if (s.ai_completion_enabled == false)
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("request_skipped"),
            QStringLiteral(
                "Skipped ghost-text request because completion is disabled in settings."),
            {QStringLiteral("file=%1").arg(
                file_name.isEmpty() == false ? file_name : QStringLiteral("<untitled>"))},
            file_name);
        return;
    }

    const pending_completion_snapshot_t snapshot = this->pending_snapshots.value(
        editor, pending_completion_snapshot_t{editor->textCursor().position(), {}, {}});
    const int pos = snapshot.cursor_position;
    if (((pos != this->last_edit_positions.value(editor, pos)) == true))
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("request_skipped"),
            QStringLiteral(
                "Skipped ghost-text request because a newer edit replaced the snapshot."),
            {QStringLiteral("file=%1").arg(
                 file_name.isEmpty() == false ? file_name : QStringLiteral("<untitled>")),
             QStringLiteral("snapshot_position=%1").arg(pos),
             QStringLiteral("last_edit_position=%1")
                 .arg(this->last_edit_positions.value(editor, pos))},
            file_name);
        return;
    }
    if (editor->textCursor().position() != pos)
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("request_skipped"),
            QStringLiteral("Skipped ghost-text request because the cursor moved before debounce "
                           "expired."),
            {QStringLiteral("file=%1").arg(
                 file_name.isEmpty() == false ? file_name : QStringLiteral("<untitled>")),
             QStringLiteral("snapshot_position=%1").arg(pos),
             QStringLiteral("current_cursor_position=%1").arg(editor->textCursor().position())},
            file_name);
        return;
    }

    if (snapshot.prefix.isEmpty() == true && snapshot.suffix.isEmpty() == true)
    {
        append_runtime_completion_event(
            this->chat_context_manager, QStringLiteral("GhostText"),
            QStringLiteral("request_skipped"),
            QStringLiteral("Skipped ghost-text request because the pending snapshot is empty."),
            {QStringLiteral("file=%1").arg(
                 file_name.isEmpty() == false ? file_name : QStringLiteral("<untitled>")),
             QStringLiteral("snapshot_position=%1").arg(pos)},
            file_name);
        return;
    }

    const QString prefix = snapshot.prefix;
    const QString suffix = snapshot.suffix;
    const QString fileName = file_name.isEmpty() == false ? file_name : QStringLiteral("untitled");
    const QString project_root = project_root_for_file_path(fileName);
    const QString log_workspace_root = project_root;

    const completion_request_settings_t completion_settings =
        resolve_completion_request_settings(s);
    const completion_connection_settings_t connection_settings =
        resolve_completion_connection_settings(s);
    const QString connection_summary =
        describe_completion_connection_settings(connection_settings);
    const QString model = completion_settings.model;
    const bool detailed_completion_logging_enabled = s.detailed_completion_logging;
    const QString completion_request_id =
        QStringLiteral("completion-%1").arg(s_next_completion_request_id.fetch_add(1));
    append_system_diagnostics_event(
        log_workspace_root, QStringLiteral("GhostText"), QStringLiteral("request.prepared"),
        QStringLiteral("Prepared ghost-text completion request."),
        {QStringLiteral("request_id=%1").arg(completion_request_id),
         QStringLiteral("detailed_completion_logging=%1")
             .arg(detailed_completion_logging_enabled == true ? QStringLiteral("on")
                                                              : QStringLiteral("off")),
         QStringLiteral("project_root=%1").arg(project_root),
         QStringLiteral("workspace_root=%1").arg(log_workspace_root),
         QStringLiteral("file=%1").arg(fileName),
         QStringLiteral("provider=%1").arg(provider->id()), QStringLiteral("model=%1").arg(model),
         QStringLiteral("connection=%1").arg(connection_summary)});

    const QString prompt = build_completion_prompt(fileName, prefix, suffix);

    QList<chat_message_t> messages;
    QString instruction_error;
    QString clang_completion_context;
    QString clang_completion_context_error;
    QString completion_context;
    QString completion_context_error;
    const QString autocomplete_instruction =
        read_autocomplete_prompt(project_root, &instruction_error);
    if (instruction_error.isEmpty() == false)
    {
        QCAI_WARN("GhostText",
                  QStringLiteral("Failed to load AUTOCOMPLETE.md: %1").arg(instruction_error));
    }
    else if (autocomplete_instruction.isEmpty() == false)
    {
        messages.append({QStringLiteral("system"), autocomplete_instruction});
    }
    clang_completion_context = build_completion_clang_context_block(
        this->clangd_service,
        editor->textDocument() != nullptr ? editor->textDocument()->filePath() : Utils::FilePath(),
        editor->document(), pos, &clang_completion_context_error);
    if (clang_completion_context_error.isEmpty() == false)
    {
        QCAI_WARN("GhostText", QStringLiteral("Failed to build clang completion context: %1")
                                   .arg(clang_completion_context_error));
    }
    else if (clang_completion_context.isEmpty() == false)
    {
        messages.append({QStringLiteral("system"), clang_completion_context});
    }
    std::shared_ptr<completion_detailed_log_t> detailed_log;
    if (this->chat_context_manager != nullptr)
    {
        completion_context = this->chat_context_manager->build_completion_context_block(
            fileName, 128, &completion_context_error);
        if (completion_context_error.isEmpty() == false)
        {
            QCAI_WARN("GhostText", QStringLiteral("Failed to build completion context: %1")
                                       .arg(completion_context_error));
        }
        else if (completion_context.isEmpty() == false)
        {
            messages.append({QStringLiteral("system"), completion_context});
        }
    }
    const QString thinking_instruction = completion_thinking_instruction(completion_settings);
    if (thinking_instruction.isEmpty() == false)
    {
        messages.append({QStringLiteral("system"), thinking_instruction});
    }
    messages.append({QStringLiteral("user"), prompt});
    if (detailed_completion_logging_enabled == true)
    {
        detailed_log = std::make_shared<completion_detailed_log_t>();
        detailed_log->begin_request(
            log_workspace_root, completion_request_id, provider->id(), model,
            completion_reasoning_effort_to_send(completion_settings), fileName, pos,
            static_cast<int>(prefix.length()), static_cast<int>(suffix.length()),
            provider->id() == QStringLiteral("copilot")
                ? qint64(settings().copilot_completion_timeout_sec) * 1000
                : -1,
            prefix, suffix, prompt, completion_prompt_parts(messages));
        append_system_diagnostics_event(
            log_workspace_root, QStringLiteral("GhostText"), QStringLiteral("request.log_started"),
            QStringLiteral("Initialized ghost-text detailed completion log."),
            {QStringLiteral("request_id=%1").arg(completion_request_id),
             QStringLiteral("target_path=%1").arg(detailed_log->log_file_path())});
        detailed_log->record_stage(QStringLiteral("local"),
                                   QStringLiteral("ghost_text.request_started"),
                                   QStringLiteral("file=%1 prefix_chars=%2 suffix_chars=%3")
                                       .arg(fileName)
                                       .arg(prefix.length())
                                       .arg(suffix.length()));
        if (instruction_error.isEmpty() == false)
        {
            detailed_log->record_stage(QStringLiteral("local"),
                                       QStringLiteral("instructions.error"), instruction_error);
        }
        else if (autocomplete_instruction.isEmpty() == false)
        {
            detailed_log->record_stage(QStringLiteral("local"),
                                       QStringLiteral("instructions.loaded"),
                                       QStringLiteral("chars=%1 source=AUTOCOMPLETE.md")
                                           .arg(autocomplete_instruction.size()));
        }
        else
        {
            detailed_log->record_stage(QStringLiteral("local"),
                                       QStringLiteral("instructions.empty"),
                                       QStringLiteral("AUTOCOMPLETE.md not found"));
        }
        if (clang_completion_context_error.isEmpty() == false)
        {
            detailed_log->record_stage(QStringLiteral("local"),
                                       QStringLiteral("clang_context.error"),
                                       clang_completion_context_error);
        }
        else if (clang_completion_context.isEmpty() == false)
        {
            detailed_log->record_stage(
                QStringLiteral("local"), QStringLiteral("clang_context.loaded"),
                QStringLiteral("chars=%1").arg(clang_completion_context.size()));
        }
        else
        {
            detailed_log->record_stage(QStringLiteral("local"),
                                       QStringLiteral("clang_context.empty"));
        }
        if (this->chat_context_manager != nullptr)
        {
            if (completion_context_error.isEmpty() == false)
            {
                detailed_log->record_stage(QStringLiteral("local"),
                                           QStringLiteral("chat_context.error"),
                                           completion_context_error);
            }
            else if (completion_context.isEmpty() == false)
            {
                detailed_log->record_stage(
                    QStringLiteral("local"), QStringLiteral("chat_context.loaded"),
                    QStringLiteral("chars=%1").arg(completion_context.size()));
            }
            else
            {
                detailed_log->record_stage(QStringLiteral("local"),
                                           QStringLiteral("chat_context.empty"));
            }
        }
        detailed_log->record_stage(
            QStringLiteral("local"), QStringLiteral("completion.settings"),
            QStringLiteral("engine=%1 provider=%2 model=%3 send_reasoning=%4 "
                           "selected_reasoning=%5 send_thinking=%6 selected_thinking=%7")
                .arg(completion_settings.engine_kind == completion_engine_kind_t::QOMPI
                         ? QStringLiteral("qompi")
                         : QStringLiteral("legacy"),
                     provider->id(), model,
                     completion_settings.send_reasoning == true ? QStringLiteral("on")
                                                                : QStringLiteral("off"),
                     completion_settings.selected_reasoning_effort,
                     completion_settings.send_thinking == true ? QStringLiteral("on")
                                                               : QStringLiteral("off"),
                     completion_settings.selected_thinking_level));
        detailed_log->record_stage(QStringLiteral("local"),
                                   QStringLiteral("completion.connection"), connection_summary);
        detailed_log->record_stage(
            QStringLiteral("local"), QStringLiteral("prompt.ready"),
            QStringLiteral("messages=%1 prompt_chars=%2").arg(messages.size()).arg(prompt.size()));
    }
    else
    {
        append_system_diagnostics_event(
            log_workspace_root, QStringLiteral("GhostText"), QStringLiteral("request.log_skipped"),
            QStringLiteral(
                "Skipped ghost-text detailed completion log because the setting is disabled."),
            {QStringLiteral("request_id=%1").arg(completion_request_id)});
    }

    QCAI_DEBUG("GhostText", QStringLiteral("Requesting completion at pos %1").arg(pos));

    const auto alive = this->alive;
    const QPointer<TextEditor::TextEditorWidget> editorGuard(editor);
    if (completion_settings.engine_kind == completion_engine_kind_t::QOMPI)
    {
        qompi::completion_request_t request;
        request.request_id = to_std_string(completion_request_id);
        request.prefix = to_std_string(prefix);
        request.suffix = to_std_string(suffix);
        request.file_path = to_std_string(fileName);
        request.user_prompt = to_std_string(prompt);
        request.trigger_kind = qompi::completion_trigger_kind_t::AUTOMATIC;
        for (const chat_message_t &message : messages)
        {
            if (message.role == QStringLiteral("system"))
            {
                request.system_context_blocks.push_back(to_std_string(message.content));
            }
        }

        qompi::completion_options_t options;
        options.model = to_std_string(model);
        options.thinking_level = to_std_string(completion_settings.selected_thinking_level);
        options.reasoning_effort =
            to_std_string(completion_reasoning_effort_to_send(completion_settings));
        const qompi::provider_profile_t provider_profile =
            create_qompi_provider_profile(provider, model);
        const qompi::resolved_completion_config_t resolved_config =
            qompi::resolve_completion_config(provider_profile, request, std::move(options));

        const QString provider_id = provider->id();
        if (this->qompi_session_provider_ids.value(editor) != provider_id ||
            this->qompi_sessions.contains(editor) == false)
        {
            this->qompi_sessions.insert(editor, qompi::create_latest_wins_completion_session(
                                                    create_qompi_provider_backend(provider)));
            this->qompi_session_provider_ids.insert(editor, provider_id);
        }
        if (detailed_log != nullptr)
        {
            detailed_log->record_stage(
                QStringLiteral("local"), QStringLiteral("qompi.dispatch_started"),
                QStringLiteral("provider=%1 model=%2 reasoning=%3 thinking=%4 "
                               "streaming=%5 cancellation=%6 stop_words=%7 max_output_tokens=%8")
                    .arg(provider->id(), from_std_string(resolved_config.model_id),
                         resolved_config.options.reasoning_effort.empty() == true
                             ? QStringLiteral("-")
                             : from_std_string(resolved_config.options.reasoning_effort),
                         resolved_config.options.thinking_level.empty() == true
                             ? QStringLiteral("-")
                             : from_std_string(resolved_config.options.thinking_level),
                         resolved_config.capabilities.supports_streaming == true
                             ? QStringLiteral("on")
                             : QStringLiteral("off"),
                         resolved_config.capabilities.supports_cancellation == true
                             ? QStringLiteral("on")
                             : QStringLiteral("off"),
                         QString::number(resolved_config.stop_policy.stop_words.size()),
                         resolved_config.options.max_output_tokens.has_value() == true
                             ? QString::number(resolved_config.options.max_output_tokens.value())
                             : QStringLiteral("-")));
        }
        const auto sink = std::make_shared<ghost_text_qompi_sink_t>(this, editorGuard, pos, prefix,
                                                                    suffix, alive, detailed_log);
        this->qompi_sessions.value(editor)->request_completion(request, resolved_config.options,
                                                               sink);
        return;
    }

    const auto streaming_state = std::make_shared<streaming_completion_state_t>();
    const iai_provider_t::completion_callback_t completion_callback =
        [this, editorGuard, pos, prefix, suffix, alive, detailed_log, streaming_state](
            const QString &response, const QString &error, const provider_usage_t &usage) {
            if (*alive == false)
            {
                finalize_detailed_completion_log(
                    detailed_log, QStringLiteral("abandoned"), response,
                    QStringLiteral("Ghost-text manager destroyed before completion callback."),
                    usage);
                return;
            }
            if (editorGuard.isNull() == true)
            {
                finalize_detailed_completion_log(
                    detailed_log, QStringLiteral("abandoned"), response,
                    QStringLiteral("Editor destroyed before completion "
                                   "callback."),
                    usage);
                return;
            }
            this->handle_completion_response(editorGuard.data(), pos, prefix, suffix, response,
                                             error, usage, detailed_log);
        };
    const iai_provider_t::stream_callback_t stream_callback = [alive](const QString &delta) {
        Q_UNUSED(alive);
        Q_UNUSED(delta);
    };
    const iai_provider_t::progress_callback_t progress_callback =
        [this, alive, editorGuard, pos, prefix, suffix, detailed_log,
         streaming_state](const provider_raw_event_t &event) {
            if (*alive == false)
            {
                return;
            }
            if (detailed_log != nullptr)
            {
                detailed_log->record_provider_event(event);
            }
            if (event.kind != provider_raw_event_kind_t::MESSAGE_DELTA || event.message.isEmpty())
            {
                return;
            }
            if (editorGuard.isNull() == true)
            {
                return;
            }

            streaming_state->accumulated_completion += event.message;
            const QString normalized_completion = normalize_completion_response(
                streaming_state->accumulated_completion, prefix, suffix);
            const QString stable_completion =
                stable_streaming_completion_prefix(normalized_completion);
            if (stable_completion.isEmpty() == true ||
                stable_completion == streaming_state->last_rendered_completion)
            {
                return;
            }
            if (this->show_completion_suggestion(editorGuard.data(), pos, prefix, suffix,
                                                 stable_completion, {}, detailed_log,
                                                 false) == false)
            {
                return;
            }

            streaming_state->last_rendered_completion = stable_completion;
            record_streaming_stage(detailed_log, QStringLiteral("streaming.partial_ready"),
                                   QStringLiteral("completion_chars=%1 rendered_chars=%2")
                                       .arg(normalized_completion.length())
                                       .arg(stable_completion.length()));
        };
    if (detailed_log != nullptr)
    {
        detailed_log->record_stage(
            QStringLiteral("local"), QStringLiteral("provider.dispatch_started"),
            QStringLiteral("provider=%1 model=%2").arg(provider->id(), model));
    }
    provider->complete(messages, model, 0.0, 128,
                       completion_reasoning_effort_to_send(completion_settings),
                       completion_callback, stream_callback, progress_callback);
}

void ghost_text_manager_t::handle_completion_response(
    TextEditor::TextEditorWidget *editor, int pos, const QString &prefix, const QString &suffix,
    const QString &response, const QString &error, const provider_usage_t &usage,
    const std::shared_ptr<completion_detailed_log_t> &detailed_log)
{
    if (detailed_log != nullptr)
    {
        detailed_log->record_stage(
            QStringLiteral("local"), QStringLiteral("provider.callback_received"),
            QStringLiteral("response_chars=%1 error=%2")
                .arg(response.size())
                .arg(error.isEmpty() == true ? QStringLiteral("<none>") : error));
    }
    if (error.isEmpty() == false)
    {
        QCAI_WARN("GhostText", QStringLiteral("Error: %1").arg(error));
        finalize_detailed_completion_log(detailed_log, QStringLiteral("error"), response, error,
                                         usage);
        return;
    }

    const QString completion = this->clean_completion(response, prefix, suffix);
    if (completion.isEmpty() == true)
    {
        finalize_detailed_completion_log(detailed_log, QStringLiteral("empty_response"), response,
                                         {}, usage);
        return;
    }

    if (this->show_completion_suggestion(editor, pos, prefix, suffix, completion, usage,
                                         detailed_log, true) == false)
    {
        return;
    }

    finalize_detailed_completion_log(detailed_log, QStringLiteral("success"), completion, {},
                                     usage);
}

bool ghost_text_manager_t::show_completion_suggestion(
    TextEditor::TextEditorWidget *editor, int pos, const QString &prefix, const QString &suffix,
    const QString &completion, const provider_usage_t &usage,
    const std::shared_ptr<completion_detailed_log_t> &detailed_log, const bool final_update)
{
    if (editor->textCursor().position() != pos)
    {
        if (final_update == true)
        {
            finalize_detailed_completion_log(
                detailed_log, QStringLiteral("stale_cursor"), completion,
                QStringLiteral("Cursor moved before ghost-text completion "
                               "was shown."),
                usage);
        }
        return false;
    }

    QTextCursor tc = editor->textCursor();
    const int line = tc.blockNumber() + 1;
    const int col = tc.positionInBlock();
    const QString line_prefix = current_line_prefix_before_cursor(prefix);
    const QString line_suffix = current_line_suffix_after_cursor(suffix);
    const QString suggestion_text = build_inline_completion_text(prefix, completion, suffix);
    const int range_start_column = col - static_cast<int>(line_prefix.size());
    if (range_start_column < 0)
    {
        if (final_update == true)
        {
            finalize_detailed_completion_log(
                detailed_log, QStringLiteral("stale_cursor"), completion,
                QStringLiteral("Current line prefix length exceeded the live cursor column."),
                usage);
        }
        return false;
    }

    Utils::Text::Position range_begin{line, range_start_column};
    const completion_text_extent_t suggestion_extent =
        measure_completion_text_extent(suggestion_text, range_start_column);
    Utils::Text::Position range_end{line + suggestion_extent.end_line_offset,
                                    suggestion_extent.end_column};
    Utils::Text::Position text_pos{line, col};
    Utils::Text::Range range{range_begin, range_end};
    TextEditor::TextSuggestion::Data data{range, text_pos, suggestion_text};

    editor->insertSuggestion(
        std::make_unique<TextEditor::TextSuggestion>(data, editor->document()));

    QCAI_DEBUG("GhostText",
               QStringLiteral("Showing suggestion: %1 chars").arg(completion.length()));
    if (detailed_log != nullptr)
    {
        detailed_log->record_stage(QStringLiteral("local"),
                                   final_update == true
                                       ? QStringLiteral("proposal.ready")
                                       : QStringLiteral("streaming.proposal.updated"),
                                   QStringLiteral("completion_chars=%1 suggestion_chars=%2 "
                                                  "line_prefix_chars=%3 line_suffix_chars=%4 "
                                                  "suggestion_lines=%5")
                                       .arg(completion.length())
                                       .arg(suggestion_text.length())
                                       .arg(line_prefix.length())
                                       .arg(line_suffix.length())
                                       .arg(suggestion_extent.end_line_offset + 1));
    }
    return true;
}

QString ghost_text_manager_t::clean_completion(const QString &raw, const QString &prefix,
                                               const QString &suffix)
{
    return normalize_completion_response(raw, prefix, suffix);
}

}  // namespace qcai2
