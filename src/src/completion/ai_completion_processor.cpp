/*! @file
    @brief Implements asynchronous AI completion proposal generation.
*/

#include "ai_completion_processor.h"
#include "../context/chat_context_manager.h"
#include "../models/agent_messages.h"
#include "../providers/iai_provider.h"
#include "../settings/settings.h"
#include "../util/logger.h"
#include "../util/project_root_resolver.h"
#include "../util/prompt_instructions.h"
#include "../util/system_diagnostics_log.h"
#include "completion_request_settings.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>

#include <QTextDocument>

#include <QFileInfo>

#include <atomic>
#include <functional>

namespace qcai2
{

static const int k_context_before = 2000;  // chars before cursor
static const int k_context_after = 500;    // chars after cursor

namespace
{

std::atomic<int> s_next_completion_request_id{1};

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

}  // namespace

ai_completion_processor_t::ai_completion_processor_t(iai_provider_t *provider,
                                                     chat_context_manager_t *chat_context_manager,
                                                     const QString &model)
    : provider(provider), model(model), chat_context_manager(chat_context_manager)
{
}

ai_completion_processor_t::~ai_completion_processor_t()
{
    if (this->detailed_log_started == true && this->detailed_log_finished == false)
    {
        this->finalize_completion_log(
            QStringLiteral("abandoned"), {},
            QStringLiteral("Completion processor destroyed before completion callback."), {});
    }
    *this->alive = false;
}

bool ai_completion_processor_t::running()
{
    return this->request_running;
}

void ai_completion_processor_t::cancel()
{
    this->cancelled = true;
    this->request_running = false;
    this->finalize_completion_log(QStringLiteral("cancelled"), {}, QStringLiteral("Cancelled"),
                                  {});
    // Don't call this->provider_->cancel() — it can trigger synchronous callbacks
    // that cause re-entrancy crash in Qt Creator's CodeAssistantPrivate::cancelCurrentRequest()
}

TextEditor::IAssistProposal *ai_completion_processor_t::perform()
{
    if ((this->provider == nullptr) || this->cancelled)
    {
        return nullptr;
    }

    const TextEditor::AssistInterface *iface = interface();
    if (iface == nullptr)
    {
        return nullptr;
    }

    const int pos = iface->position();
    const int start_before = qMax(0, pos - k_context_before);
    const QString prefix = iface->textAt(start_before, pos - start_before);
    const QString suffix = iface->textAt(pos, k_context_after);
    const QString file_name = iface->filePath().toUrlishString();
    const QString project_root = project_root_for_file_path(file_name);
    const QString log_workspace_root =
        project_root.isEmpty() == false ? project_root : QFileInfo(file_name).absolutePath();

    // Build FIM (fill-in-the-middle) prompt
    const QString prompt =
        QStringLiteral("You are a code completion engine. Complete the code at the cursor "
                       "position marked <CURSOR>.\n"
                       "File: %1\n"
                       "Return ONLY the completion text, no explanation, no markdown.\n\n"
                       "```\n%2<CURSOR>%3\n```")
            .arg(file_name, prefix, suffix);

    const auto &s = settings();
    const completion_request_settings_t completion_settings =
        resolve_completion_request_settings(s);
    const QString thinking_instruction = completion_thinking_instruction(completion_settings);
    QList<chat_message_t> messages;
    QString instruction_error;
    QString completion_context;
    QString completion_context_error;
    const bool detailed_completion_logging_enabled = s.detailed_completion_logging;
    const QString completion_request_id =
        QStringLiteral("completion-%1").arg(s_next_completion_request_id.fetch_add(1));
    append_system_diagnostics_event(
        log_workspace_root, QStringLiteral("Completion"), QStringLiteral("request.prepared"),
        QStringLiteral("Prepared assist-processor completion request."),
        {QStringLiteral("request_id=%1").arg(completion_request_id),
         QStringLiteral("detailed_completion_logging=%1")
             .arg(detailed_completion_logging_enabled == true ? QStringLiteral("on")
                                                              : QStringLiteral("off")),
         QStringLiteral("project_root=%1").arg(project_root),
         QStringLiteral("workspace_root=%1").arg(log_workspace_root),
         QStringLiteral("file=%1").arg(file_name),
         QStringLiteral("provider=%1").arg(this->provider->id()),
         QStringLiteral("model=%1").arg(this->model)});
    const QString autocomplete_instruction =
        read_autocomplete_prompt(project_root, &instruction_error);
    if (instruction_error.isEmpty() == false)
    {
        QCAI_WARN("Completion",
                  QStringLiteral("Failed to load AUTOCOMPLETE.md: %1").arg(instruction_error));
    }
    else if (autocomplete_instruction.isEmpty() == false)
    {
        messages.append({QStringLiteral("system"), autocomplete_instruction});
    }
    if (this->chat_context_manager != nullptr)
    {
        completion_context = this->chat_context_manager->build_completion_context_block(
            file_name, 128, &completion_context_error);
        if (completion_context_error.isEmpty() == false)
        {
            QCAI_WARN("Completion", QStringLiteral("Failed to build completion context: %1")
                                        .arg(completion_context_error));
        }
        else if (completion_context.isEmpty() == false)
        {
            messages.append({QStringLiteral("system"), completion_context});
        }
    }
    if (thinking_instruction.isEmpty() == false)
    {
        messages.append({QStringLiteral("system"), thinking_instruction});
    }
    messages.append({QStringLiteral("user"), prompt});
    this->detailed_log_started = false;
    this->detailed_log_finished = false;
    if (detailed_completion_logging_enabled == true)
    {
        this->detailed_log.begin_request(
            log_workspace_root, completion_request_id, this->provider->id(), this->model,
            completion_reasoning_effort_to_send(completion_settings), file_name, pos,
            static_cast<int>(prefix.length()), static_cast<int>(suffix.length()),
            this->provider->id() == QStringLiteral("copilot")
                ? qint64(s.copilot_completion_timeout_sec) * 1000
                : -1,
            prefix, suffix, prompt, completion_prompt_parts(messages));
        this->detailed_log_started = true;
        append_system_diagnostics_event(
            log_workspace_root, QStringLiteral("Completion"),
            QStringLiteral("request.log_started"),
            QStringLiteral("Initialized assist-processor detailed completion log."),
            {QStringLiteral("request_id=%1").arg(completion_request_id),
             QStringLiteral("target_path=%1").arg(this->detailed_log.log_file_path())});
        this->detailed_log.record_stage(QStringLiteral("local"),
                                        QStringLiteral("processor.perform_started"),
                                        QStringLiteral("file=%1 prefix_chars=%2 suffix_chars=%3")
                                            .arg(file_name)
                                            .arg(prefix.length())
                                            .arg(suffix.length()));
        if (instruction_error.isEmpty() == false)
        {
            this->detailed_log.record_stage(
                QStringLiteral("local"), QStringLiteral("instructions.error"), instruction_error);
        }
        else if (autocomplete_instruction.isEmpty() == false)
        {
            this->detailed_log.record_stage(QStringLiteral("local"),
                                            QStringLiteral("instructions.loaded"),
                                            QStringLiteral("chars=%1 source=AUTOCOMPLETE.md")
                                                .arg(autocomplete_instruction.size()));
        }
        else
        {
            this->detailed_log.record_stage(QStringLiteral("local"),
                                            QStringLiteral("instructions.empty"),
                                            QStringLiteral("AUTOCOMPLETE.md not found"));
        }
        if (this->chat_context_manager != nullptr)
        {
            if (completion_context_error.isEmpty() == false)
            {
                this->detailed_log.record_stage(QStringLiteral("local"),
                                                QStringLiteral("chat_context.error"),
                                                completion_context_error);
            }
            else if (completion_context.isEmpty() == false)
            {
                this->detailed_log.record_stage(
                    QStringLiteral("local"), QStringLiteral("chat_context.loaded"),
                    QStringLiteral("chars=%1").arg(completion_context.size()));
            }
            else
            {
                this->detailed_log.record_stage(QStringLiteral("local"),
                                                QStringLiteral("chat_context.empty"));
            }
        }
        this->detailed_log.record_stage(
            QStringLiteral("local"), QStringLiteral("completion.settings"),
            QStringLiteral("provider=%1 model=%2 send_reasoning=%3 selected_reasoning=%4 "
                           "send_thinking=%5 selected_thinking=%6")
                .arg(this->provider->id(), this->model,
                     completion_settings.send_reasoning == true ? QStringLiteral("on")
                                                                : QStringLiteral("off"),
                     completion_settings.selected_reasoning_effort,
                     completion_settings.send_thinking == true ? QStringLiteral("on")
                                                               : QStringLiteral("off"),
                     completion_settings.selected_thinking_level));
        this->detailed_log.record_stage(
            QStringLiteral("local"), QStringLiteral("prompt.ready"),
            QStringLiteral("messages=%1 prompt_chars=%2").arg(messages.size()).arg(prompt.size()));
    }
    else
    {
        append_system_diagnostics_event(
            log_workspace_root, QStringLiteral("Completion"),
            QStringLiteral("request.log_skipped"),
            QStringLiteral("Skipped assist-processor detailed completion log because the setting "
                           "is disabled."),
            {QStringLiteral("request_id=%1").arg(completion_request_id)});
    }

    this->request_running = true;
    QCAI_DEBUG("Completion",
               QStringLiteral(
                   "Requesting completion at pos %1 in %2 (prefix: %3 chars, suffix: %4 chars)")
                   .arg(pos)
                   .arg(file_name)
                   .arg(prefix.length())
                   .arg(suffix.length()));

    using namespace std::placeholders;
    const auto alive = this->alive;
    const iai_provider_t::completion_callback_t completion_callback = std::bind(
        &ai_completion_processor_t::dispatch_completion_response, this, pos, alive, _1, _2, _3);
    const iai_provider_t::progress_callback_t progress_callback =
        std::bind(&ai_completion_processor_t::dispatch_progress_event, this, alive, _1);
    if (this->detailed_log_started == true)
    {
        this->detailed_log.record_stage(
            QStringLiteral("local"), QStringLiteral("provider.dispatch_started"),
            QStringLiteral("provider=%1 model=%2").arg(this->provider->id(), this->model));
    }
    this->provider->complete(messages, this->model, 0.0, 128,
                             completion_reasoning_effort_to_send(completion_settings),
                             completion_callback, nullptr, progress_callback);

    return nullptr;  // async — proposal delivered via setAsyncProposalAvailable
}

void ai_completion_processor_t::dispatch_completion_response(
    ai_completion_processor_t *processor, int pos, const std::shared_ptr<bool> &alive,
    const QString &response, const QString &error, const provider_usage_t &usage)
{
    if (*alive == false)
    {
        return;
    }

    processor->handle_completion_response(pos, response, error, usage);
}

void ai_completion_processor_t::dispatch_progress_event(ai_completion_processor_t *processor,
                                                        const std::shared_ptr<bool> &alive,
                                                        const provider_raw_event_t &event)
{
    if (*alive == false)
    {
        return;
    }

    processor->handle_progress_event(event);
}

void ai_completion_processor_t::handle_completion_response(int pos, const QString &response,
                                                           const QString &error,
                                                           const provider_usage_t &usage)
{
    this->request_running = false;
    if (this->detailed_log_started == true)
    {
        this->detailed_log.record_stage(
            QStringLiteral("local"), QStringLiteral("provider.callback_received"),
            QStringLiteral("response_chars=%1 error=%2")
                .arg(response.size())
                .arg(error.isEmpty() == true ? QStringLiteral("<none>") : error));
    }

    if (this->cancelled || !error.isEmpty() || response.trimmed().isEmpty())
    {
        if (!error.isEmpty())
        {
            QCAI_WARN("Completion", QStringLiteral("Completion error: %1").arg(error));
            this->finalize_completion_log(QStringLiteral("error"), response, error, usage);
        }
        else if (this->cancelled)
        {
            QCAI_DEBUG("Completion", QStringLiteral("Completion cancelled"));
            this->finalize_completion_log(QStringLiteral("cancelled"), response,
                                          QStringLiteral("Cancelled"), usage);
        }
        else
        {
            this->finalize_completion_log(QStringLiteral("empty_response"), response, {}, usage);
        }
        setAsyncProposalAvailable(nullptr);
        return;
    }

    QString completion = response.trimmed();
    if (completion.startsWith(QStringLiteral("```")))
    {
        const qsizetype first_nl = completion.indexOf(QLatin1Char('\n'));
        if (first_nl >= 0)
        {
            completion = completion.mid(first_nl + 1);
        }
        if (completion.endsWith(QStringLiteral("```")))
        {
            completion.chop(3);
        }
        completion = completion.trimmed();
    }

    if (completion.isEmpty() == true)
    {
        this->finalize_completion_log(QStringLiteral("empty_response"), response, {}, usage);
        setAsyncProposalAvailable(nullptr);
        return;
    }

    QList<TextEditor::AssistProposalItemInterface *> items;

    auto *item = new TextEditor::AssistProposalItem;
    const qsizetype nl_pos = completion.indexOf(QLatin1Char('\n'));
    if (nl_pos > 0)
    {
        item->setText(completion.left(nl_pos));
        item->setDetail(completion);
    }
    else
    {
        item->setText(completion);
        item->setDetail(QStringLiteral("AI completion"));
    }
    item->setData(completion);
    items.append(item);

    if (nl_pos > 0)
    {
        auto *line_item = new TextEditor::AssistProposalItem;
        line_item->setText(completion.left(nl_pos));
        line_item->setDetail(QStringLiteral("AI (first line)"));
        line_item->setData(completion.left(nl_pos));
        items.append(line_item);
    }

    auto *proposal = new TextEditor::GenericProposal(pos, items);
    QCAI_DEBUG("Completion", QStringLiteral("Completion ready: %1 items, %2 chars")
                                 .arg(items.size())
                                 .arg(completion.length()));
    if (this->detailed_log_started == true)
    {
        this->detailed_log.record_stage(QStringLiteral("local"), QStringLiteral("proposal.ready"),
                                        QStringLiteral("items=%1 completion_chars=%2")
                                            .arg(items.size())
                                            .arg(completion.length()));
    }
    this->finalize_completion_log(QStringLiteral("success"), completion, {}, usage);
    setAsyncProposalAvailable(proposal);
}

void ai_completion_processor_t::handle_progress_event(const provider_raw_event_t &event)
{
    if (this->detailed_log_started == false)
    {
        return;
    }
    this->detailed_log.record_provider_event(event);
}

void ai_completion_processor_t::finalize_completion_log(const QString &status,
                                                        const QString &response,
                                                        const QString &error,
                                                        const provider_usage_t &usage)
{
    if (this->detailed_log_started == false || this->detailed_log_finished == true)
    {
        return;
    }

    this->detailed_log_finished = true;
    this->detailed_log.finish_request(status, response, error, usage);
    QString log_error;
    if (this->detailed_log.append_to_project_log(&log_error) == false)
    {
        append_system_diagnostics_event(
            this->detailed_log.workspace_root_path(), QStringLiteral("Completion"),
            QStringLiteral("request.log_write_failed"),
            QStringLiteral("Assist-processor detailed completion log write failed."),
            {QStringLiteral("status=%1").arg(status), QStringLiteral("error=%1").arg(log_error)});
        QCAI_WARN("Completion",
                  QStringLiteral("Failed to append detailed completion log: %1").arg(log_error));
    }
    else
    {
        append_system_diagnostics_event(
            this->detailed_log.workspace_root_path(), QStringLiteral("Completion"),
            QStringLiteral("request.log_write_succeeded"),
            QStringLiteral("Assist-processor detailed completion log write succeeded."),
            {QStringLiteral("status=%1").arg(status),
             QStringLiteral("target_path=%1").arg(this->detailed_log.log_file_path())});
    }
}

}  // namespace qcai2
