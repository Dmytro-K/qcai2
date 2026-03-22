/*! @file
    @brief Implements the plan-act-observe loop that drives one agent session.
*/

#include "agent_controller.h"
#include "context/chat_context_manager.h"
#include "mcp/mcp_tool_manager.h"
#include "settings/settings.h"
#include "util/diff.h"
#include "util/logger.h"
#include "util/prompt_instructions.h"
#include "util/request_detailed_log.h"

#include "context/chat_context.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <functional>
#include <utility>

namespace qcai2
{

namespace
{

constexpr int k_default_prompt_result_limit = 15000;
constexpr int k_verbose_tool_prompt_result_limit = 4000;
constexpr int k_provider_inactivity_timeout_ms = 75000;
constexpr int k_provider_thinking_inactivity_timeout_ms = 180000;
const QString k_superseded_by_user_steer_status = QStringLiteral("superseded_by_user_steer");

QString as_indented_code_block(const QString &text)
{
    const QString normalized = text.isEmpty() ? QStringLiteral("(empty)") : text;
    QStringList lines = normalized.split(QLatin1Char('\n'));
    for (QString &line : lines)
    {
        line.prepend(QStringLiteral("    "));
    }
    return lines.join(QLatin1Char('\n'));
}

QString format_tool_args(const QJsonObject &args)
{
    return QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Indented)).trimmed();
}

QString format_tool_execution_log(const QString &name, const QJsonObject &args)
{
    return QStringLiteral("🔧 **Tool:** `%1`\n\n**Arguments**\n\n%2")
        .arg(name, as_indented_code_block(format_tool_args(args)));
}

QString format_tool_result_log(const QString &result)
{
    static constexpr int kMaxLoggedResultLength = 500;

    QString preview = result;
    if (preview.size() > kMaxLoggedResultLength)
    {
        preview = preview.left(kMaxLoggedResultLength).trimmed();
        preview += QStringLiteral("\n... [truncated, %1 chars total]").arg(result.size());
    }

    return QStringLiteral("✅ **result_t**\n\n%1").arg(as_indented_code_block(preview.trimmed()));
}

QString merge_diff_sections(const QString &left, const QString &right)
{
    const QString normalized_left = Diff::normalize(left).trimmed();
    const QString normalized_right = Diff::normalize(right).trimmed();
    if (normalized_left.isEmpty() == true)
    {
        return normalized_right;
    }
    if (normalized_right.isEmpty() == true)
    {
        return normalized_left;
    }
    return normalized_left + QStringLiteral("\n") + normalized_right;
}

int prompt_result_limit_for_tool(const QString &name)
{
    if (name == QStringLiteral("show_compile_output") ||
        name == QStringLiteral("show_application_output") ||
        name == QStringLiteral("show_diagnostics"))
    {
        return k_verbose_tool_prompt_result_limit;
    }

    return k_default_prompt_result_limit;
}

bool is_enabled_effort(const QString &level)
{
    return level == QStringLiteral("low") || level == QStringLiteral("medium") ||
           level == QStringLiteral("high");
}

QString run_mode_label(agent_controller_t::run_mode_t run_mode)
{
    return run_mode == agent_controller_t::run_mode_t::ASK ? QStringLiteral("ask")
                                                           : QStringLiteral("agent");
}

QString response_type_label(response_type_t type)
{
    switch (type)
    {
        case response_type_t::PLAN:
            return QStringLiteral("plan");
        case response_type_t::TOOL_CALL:
            return QStringLiteral("tool_call");
        case response_type_t::FINAL:
            return QStringLiteral("final");
        case response_type_t::NEED_APPROVAL:
            return QStringLiteral("need_approval");
        case response_type_t::TEXT:
            return QStringLiteral("text");
        case response_type_t::ERROR:
            return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

context_request_kind_t context_request_kind(agent_controller_t::run_mode_t run_mode)
{
    return run_mode == agent_controller_t::run_mode_t::ASK ? context_request_kind_t::ASK
                                                           : context_request_kind_t::AGENT_CHAT;
}

QString artifact_kind_for_tool(const QString &tool_name)
{
    if (tool_name == QStringLiteral("run_build"))
    {
        return QStringLiteral("build_log");
    }
    if (tool_name == QStringLiteral("run_tests"))
    {
        return QStringLiteral("test_result");
    }
    if (tool_name == QStringLiteral("git_diff") || tool_name == QStringLiteral("apply_patch"))
    {
        return QStringLiteral("diff");
    }
    if (tool_name == QStringLiteral("read_file"))
    {
        return QStringLiteral("file_snippet");
    }
    return QStringLiteral("tool_output");
}

QString soft_steer_system_note()
{
    return QStringLiteral(
        "Follow-up user input arrived during the previous in-progress turn. "
        "Treat any unfinished assistant draft as superseded_by_user_steer, prioritize the newest "
        "user message(s), and re-plan from the updated user intent instead of continuing the old "
        "path blindly.");
}

QString soft_steer_tool_completion_note(const QString &tool_name, const QString &tool_result)
{
    return QStringLiteral(
               "Before the user's follow-up arrived, tool '%1' finished with this result:\n%2\n\n"
               "Use it only if it is still relevant to the updated request.")
        .arg(tool_name, as_indented_code_block(tool_result));
}

QString soft_steer_follow_up_message(const QString &message, const QString &request_context)
{
    const QString trimmed_message = message.trimmed();
    const QString trimmed_context = request_context.trimmed();
    if (trimmed_context.isEmpty() == true)
    {
        return trimmed_message;
    }

    return QStringLiteral("%1\n\nAdditional follow-up context:\n%2")
        .arg(trimmed_message, trimmed_context);
}

QString format_soft_steer_request_log(const QString &message, const QString &request_context,
                                      const QStringList &linked_files, int index, int total_count)
{
    QStringList sections;
    const QString title =
        total_count > 1
            ? QStringLiteral("[pending] Soft steer request %1/%2").arg(index + 1).arg(total_count)
            : QStringLiteral("[pending] Soft steer request");
    sections.append(
        QStringLiteral("%1\n\n%2").arg(title, as_indented_code_block(message.trimmed())));

    const QString trimmed_context = request_context.trimmed();
    if (trimmed_context.isEmpty() == false)
    {
        sections.append(QStringLiteral("Additional context\n\n%1")
                            .arg(as_indented_code_block(trimmed_context)));
    }

    if (linked_files.isEmpty() == false)
    {
        sections.append(
            QStringLiteral("Linked files: %1").arg(linked_files.join(QStringLiteral(", "))));
    }

    return sections.join(QStringLiteral("\n\n"));
}

}  // namespace

agent_controller_t::agent_controller_t(QObject *parent) : QObject(parent), run_state_machine(this)
{
    this->provider_watchdog.setSingleShot(true);
    this->provider_watchdog.setInterval(k_provider_inactivity_timeout_ms);
    connect(&this->provider_watchdog, &QTimer::timeout, this,
            &agent_controller_t::handle_provider_inactivity_timeout);
}

void agent_controller_t::set_provider(iai_provider_t *provider)
{
    this->provider = provider;
}
void agent_controller_t::set_providers(const QList<iai_provider_t *> &providers)
{
    this->all_providers = providers;
}
void agent_controller_t::set_tool_registry(tool_registry_t *registry)
{
    this->tool_registry = registry;
}
void agent_controller_t::set_editor_context(editor_context_t *ctx)
{
    this->context_provider = ctx;
}
void agent_controller_t::set_chat_context_manager(chat_context_manager_t *manager)
{
    this->chat_context_manager = manager;
}
void agent_controller_t::set_mcp_tool_manager(mcp_tool_manager_t *manager)
{
    this->mcp_tool_manager = manager;
}
void agent_controller_t::set_safety_policy(safety_policy_t *policy)
{
    this->safety_policy = policy;
}

void agent_controller_t::set_request_context(const QString &context,
                                             const QStringList &linked_files)
{
    this->request_context = context;
    this->linked_files = linked_files;
}

void agent_controller_t::emit_run_log_message(const QString &message,
                                              bool visible_during_compaction)
{
    if (this->run_options.is_compaction == true && visible_during_compaction == false)
    {
        return;
    }
    emit this->log_message(message);
}

QString agent_controller_t::build_system_prompt() const
{
    QString sys;
    if (this->run_options.is_compaction == true)
    {
        return QStringLiteral(
            "You are compacting the existing conversation context for future requests.\n"
            "Do NOT call tools.\n"
            "Do NOT ask for approval.\n"
            "Do NOT return a diff.\n"
            "Reply with exactly one JSON object of type \"final\".\n"
            "Put the full compacted context into final.summary.\n"
            "Keep the required structure from the user's compact request and preserve important "
            "technical facts, decisions, constraints, unresolved problems, and recent relevant "
            "context.\n"
            "Return no extra prose outside the JSON object.\n\n"
            "{\"type\":\"final\",\"summary\":\"full compacted context\",\"diff\":\"\"}\n");
    }

    if (this->run_mode == run_mode_t::ASK)
    {
        sys += QStringLiteral(
            "You are an AI assistant inside Qt Creator. Answer the user's request directly using "
            "the current project/editor context. You MUST respond in JSON with exactly this "
            "shape:\n"
            "{\"type\":\"final\",\"summary\":\"direct answer for the user\",\"diff\":\"\"}\n\n"
            "Do NOT return plan, tool_call, or need_approval. Do NOT use tools. Do NOT include a "
            "diff unless the user explicitly asked for code changes.\n\n");
    }
    else
    {
        sys += QStringLiteral(
            "You are an AI coding agent inside Qt Creator. You help the user accomplish coding "
            "tasks by using available tools. You MUST respond in JSON with one of these types:\n"
            "1) {\"type\":\"plan\", \"steps\":[\"step description\", ...]}\n"
            "2) {\"type\":\"tool_call\", \"name\":\"tool_name\", \"args\":{...}}\n"
            "3) {\"type\":\"final\", \"summary\":\"what was done\", \"diff\":\"optional unified "
            "diff\"}\n"
            "4) {\"type\":\"need_approval\", \"action\":\"what\", \"reason\":\"why\", "
            "\"preview\":\"details\"}\n"
            "If the user later rejects part of an inline diff, continue the same task and reply "
            "with a new \"final\" response that preserves accepted edits and replaces only the "
            "rejected changes.\n\n");
    }

    // Available tools
    if (this->run_mode == run_mode_t::AGENT && (this->tool_registry != nullptr))
    {
        QJsonDocument toolsDoc(this->tool_registry->tool_descriptions_json());
        sys += QStringLiteral("Available tools:\n%1\n\n")
                   .arg(QString::fromUtf8(toolsDoc.toJson(QJsonDocument::Indented)));
    }

    // Safety info
    if (this->dry_run)
    {
        sys += QStringLiteral(
            "MODE: DRY-RUN. Do NOT apply patches; only produce diffs for preview.\n");
    }

    if (this->run_mode == run_mode_t::AGENT)
    {
        sys += QStringLiteral(
            "\nFor simple/single-step tasks, skip planning and use tool_call or final directly. "
            "Only produce a plan for multi-step tasks.\n");
    }
    else
    {
        sys += QStringLiteral("\nMODE: ASK. Provide a concise direct answer in final.summary.\n");
    }

    return sys;
}

void agent_controller_t::start(const QString &goal, bool dry_run, run_mode_t run_mode,
                               const QString &model_name, const QString &reasoning_effort,
                               const QString &thinking_level, const run_options_t &options)
{
    if (this->is_running())
    {
        return;
    }

    // Re-select and reconfigure provider from current settings
    auto &s = settings();
    s.load();
    logger_t::instance().set_enabled(s.debug_logging);
    if (!this->all_providers.isEmpty())
    {
        this->provider = nullptr;
        QCAI_DEBUG("Agent", QStringLiteral("Selecting provider '%1' from %2 available")
                                .arg(s.provider)
                                .arg(this->all_providers.size()));
        for (auto *p : this->all_providers)
        {
            if (p->id() == s.provider)
            {
                this->provider = p;
                break;
            }
        }
        if (this->provider == nullptr)
        {
            this->provider = this->all_providers.first();
        }

        // Reconfigure the selected provider
        if (s.provider == QStringLiteral("openai"))
        {
            this->provider->set_base_url(s.base_url);
            this->provider->set_api_key(s.api_key);
        }
        else if (s.provider == QStringLiteral("local"))
        {
            this->provider->set_base_url(s.local_base_url);
            this->provider->set_api_key(s.api_key);
        }
        else if (s.provider == QStringLiteral("ollama"))
        {
            this->provider->set_base_url(s.ollama_base_url);
        }
    }

    if (this->provider == nullptr)
    {
        emit this->error_occurred(QStringLiteral("No AI provider configured."));
        return;
    }

    QStringList mcp_refresh_messages;
    editor_context_t::snapshot_t context_snapshot;
    QString workspace_root;
    QString workspace_id;
    if (this->context_provider != nullptr)
    {
        context_snapshot = this->context_provider->capture();
        workspace_root = context_snapshot.project_dir;
        if (workspace_root.isEmpty() == true && context_snapshot.file_path.isEmpty() == false)
        {
            workspace_root = QFileInfo(context_snapshot.file_path).absolutePath();
        }
        workspace_id = context_snapshot.project_file_path;
        if (workspace_id.isEmpty() == true)
        {
            workspace_id = workspace_root;
        }
    }
    if (run_mode == run_mode_t::AGENT && this->mcp_tool_manager != nullptr)
    {
        mcp_refresh_messages =
            this->mcp_tool_manager->refresh_for_project(context_snapshot.project_dir);
    }

    this->run_state_machine.start_run();
    this->dry_run = dry_run;
    this->current_iteration = 0;
    this->current_tool_call_count = 0;
    this->text_retries = 0;
    this->mode_retries = 0;
    this->goal = goal;
    this->run_mode = run_mode;
    this->run_options = options;
    this->model_name = model_name.trimmed().isEmpty() ? s.model_name : model_name.trimmed();
    this->reasoning_effort =
        reasoning_effort.trimmed().isEmpty() ? s.reasoning_effort : reasoning_effort.trimmed();
    this->thinking_level =
        thinking_level.trimmed().isEmpty() ? s.thinking_level : thinking_level.trimmed();
    this->plan.clear();
    this->final_diff_preview.clear();
    this->accepted_inline_diff_preview.clear();
    this->pending_final_summary.clear();
    this->pending_approvals.clear();
    this->messages.clear();
    this->run_id.clear();
    this->accumulated_usage = {};
    this->pending_soft_steer_messages.clear();
    this->soft_steer_pending_flag = false;
    this->pending_soft_steer_system_note.clear();
    this->pending_provider_responses.clear();
    this->provider_response_dispatch_scheduled = false;
    this->pending_validation_tool_name.clear();
    this->pending_validation_label.clear();
    this->progress_tracker = std::make_unique<agent_progress_tracker_t>(
        this->provider != nullptr ? this->provider->id() : QString(),
        agent_status_render_mode_t::INTERACTIVE, settings().agent_debug);
    this->detailed_request_log.reset();
    QString configured_instructions_error;
    const QStringList configured_instructions = configured_system_instructions(
        workspace_root, s.system_prompt, &configured_instructions_error);
    if (configured_instructions_error.isEmpty() == false)
    {
        this->emit_run_log_message(QStringLiteral("⚠ Failed to load project rules: %1")
                                       .arg(configured_instructions_error));
    }

    QCAI_INFO("Agent",
              QStringLiteral("Starting run — mode: %1, provider: %2, model: %3, reasoning: %4, "
                             "thinking: %5, dryRun: %6, goal: %7")
                  .arg(run_mode_label(this->run_mode), this->provider->id(), this->model_name,
                       this->reasoning_effort, this->thinking_level,
                       dry_run ? QStringLiteral("yes") : QStringLiteral("no"), goal.left(100)));

    QStringList dynamic_system_messages;
    if (this->context_provider != nullptr)
    {
        const QString editor_context = this->context_provider->to_prompt_fragment().trimmed();
        if (editor_context.isEmpty() == false)
        {
            dynamic_system_messages.append(
                QStringLiteral("Current editor context:\n%1").arg(editor_context));
        }

        const QString open_file_contents =
            this->context_provider->file_contents_fragment(30000).trimmed();
        if (open_file_contents.isEmpty() == false)
        {
            dynamic_system_messages.append(
                QStringLiteral("File contents (from open tabs):\n%1").arg(open_file_contents));
        }
    }
    if (this->request_context.trimmed().isEmpty() == false)
    {
        dynamic_system_messages.append(this->request_context.trimmed());
    }
    if (is_enabled_effort(this->thinking_level) == true)
    {
        dynamic_system_messages.append(
            QStringLiteral("Use %1 thinking depth for this task.").arg(this->thinking_level));
    }

    if (s.detailed_request_logging == true && workspace_root.isEmpty() == false)
    {
        this->detailed_request_log = std::make_unique<request_detailed_log_t>();
        this->detailed_request_log->begin_request(
            workspace_root, {}, this->goal, this->request_context.trimmed(), this->linked_files,
            this->provider->id(), this->model_name, this->reasoning_effort, this->thinking_level,
            run_mode_label(this->run_mode), this->dry_run, mcp_refresh_messages);
    }

    bool restoredPersistentContext = false;
    if ((this->chat_context_manager != nullptr) && (this->context_provider != nullptr))
    {
        if (workspace_root.isEmpty() == false && workspace_id.isEmpty() == false)
        {
            QString contextError;
            const QString existingConversationId =
                this->chat_context_manager->active_workspace_id() == workspace_id
                    ? this->chat_context_manager->active_conversation_id()
                    : QString();
            if (this->chat_context_manager->set_active_workspace(
                    workspace_id, workspace_root, existingConversationId, &contextError) == true)
            {
                this->chat_context_manager->sync_workspace_state(context_snapshot, s,
                                                                 &contextError);
                if (contextError.isEmpty() == true)
                {
                    const QJsonObject runMetadata{
                        {QStringLiteral("goalPreview"), this->run_options.is_compaction == true
                                                            ? QStringLiteral("[compaction]")
                                                            : goal.left(200)},
                        {QStringLiteral("projectFilePath"), context_snapshot.project_file_path},
                        {QStringLiteral("projectDir"), context_snapshot.project_dir},
                        {QStringLiteral("linkedFileCount"), this->linked_files.size()},
                        {QStringLiteral("mode"), run_mode_label(this->run_mode)},
                        {QStringLiteral("compaction"), this->run_options.is_compaction},
                    };
                    this->run_id = this->chat_context_manager->begin_run(
                        this->run_options.is_compaction == true
                            ? context_request_kind_t::AGENT_CHAT
                            : context_request_kind(this->run_mode),
                        this->provider->id(), this->model_name, this->reasoning_effort,
                        this->thinking_level, this->dry_run, runMetadata, &contextError);
                    if (contextError.isEmpty() == true && this->detailed_request_log != nullptr)
                    {
                        this->detailed_request_log->set_run_id(this->run_id);
                    }
                }

                if (this->run_options.is_compaction == false && contextError.isEmpty() == true &&
                    this->request_context.trimmed().isEmpty() == false)
                {
                    const QJsonObject requestContextMetadata{
                        {QStringLiteral("linkedFiles"),
                         QJsonArray::fromStringList(this->linked_files)},
                    };
                    this->chat_context_manager->append_artifact(
                        this->run_id, QStringLiteral("request_context"),
                        QStringLiteral("request_context"), this->request_context.trimmed(),
                        requestContextMetadata, &contextError);
                }

                if (this->run_options.is_compaction == false && contextError.isEmpty() == true)
                {
                    const QJsonObject goalMetadata{
                        {QStringLiteral("mode"), run_mode_label(this->run_mode)},
                        {QStringLiteral("linkedFileCount"), this->linked_files.size()},
                    };
                    this->chat_context_manager->append_user_message(
                        this->run_id, goal, QStringLiteral("goal"), goalMetadata, &contextError);
                }

                if (contextError.isEmpty() == true)
                {
                    const context_envelope_t envelope =
                        this->chat_context_manager->build_context_envelope(
                            context_request_kind(this->run_mode), this->build_system_prompt(),
                            dynamic_system_messages, s.max_tokens, &contextError);
                    if (contextError.isEmpty() == true &&
                        envelope.provider_messages.isEmpty() == false)
                    {
                        this->messages = envelope.provider_messages;
                        for (auto it = configured_instructions.crbegin();
                             it != configured_instructions.crend(); ++it)
                        {
                            this->messages.prepend({QStringLiteral("system"), *it});
                        }
                        if (this->run_options.is_compaction == true)
                        {
                            this->messages.append({QStringLiteral("user"), goal});
                        }
                        restoredPersistentContext = true;
                    }
                }
            }

            if (contextError.isEmpty() == false)
            {
                if ((this->chat_context_manager != nullptr) && (this->run_id.isEmpty() == false))
                {
                    QString finishError;
                    this->chat_context_manager->finish_run(
                        this->run_id, QStringLiteral("error"), this->accumulated_usage,
                        QJsonObject{{QStringLiteral("error"), contextError},
                                    {QStringLiteral("reason"),
                                     QStringLiteral("context-bootstrap-failed")}},
                        &finishError);
                }
                this->emit_run_log_message(
                    QStringLiteral("⚠ Persistent chat context unavailable: %1").arg(contextError));
                this->run_id.clear();
            }
        }
    }

    if (restoredPersistentContext == false)
    {
        for (const QString &instruction : configured_instructions)
        {
            this->messages.append({QStringLiteral("system"), instruction});
        }
        this->messages.append({QStringLiteral("system"), this->build_system_prompt()});

        for (const QString &message : dynamic_system_messages)
        {
            this->messages.append({QStringLiteral("system"), message});
        }

        this->messages.append({QStringLiteral("user"), goal});
    }

    if (this->run_options.is_compaction == true)
    {
        this->emit_run_log_message(QStringLiteral("🗜 Compaction started."), true);
    }
    else
    {
        this->emit_run_log_message(
            QStringLiteral("%1 %2 started. Goal: %3")
                .arg(this->run_mode == run_mode_t::ASK ? QStringLiteral("💬")
                                                       : QStringLiteral("▶"))
                .arg(this->run_mode == run_mode_t::ASK ? QStringLiteral("Ask mode")
                                                       : QStringLiteral("Agent"))
                .arg(goal),
            true);
        if (!this->linked_files.isEmpty())
        {
            this->emit_run_log_message(QStringLiteral("📎 Linked files: %1")
                                           .arg(this->linked_files.join(QStringLiteral(", "))),
                                       true);
        }
        for (const QString &message : std::as_const(mcp_refresh_messages))
        {
            emit log_message(message);
        }
    }
    this->run_next_iteration();
}

bool agent_controller_t::enqueue_soft_steer_message(const QString &message,
                                                    const QString &request_context,
                                                    const QStringList &linked_files)
{
    const QString trimmed_message = message.trimmed();
    if (this->is_running() == false || trimmed_message.isEmpty() == true)
    {
        return false;
    }

    this->pending_soft_steer_messages.append(
        {trimmed_message, request_context.trimmed(), linked_files});
    this->soft_steer_pending_flag = true;

    emit this->soft_steer_pending(static_cast<int>(this->pending_soft_steer_messages.size()));
    emit this->status_changed(
        QStringLiteral("Soft steer pending (%1 queued follow-up message%2).")
            .arg(this->pending_soft_steer_messages.size())
            .arg(this->pending_soft_steer_messages.size() == 1 ? QString() : QStringLiteral("s")));

    if (this->run_state_machine.is_waiting_for_approval() == true ||
        this->run_state_machine.is_waiting_for_inline_diff_review() == true)
    {
        QTimer::singleShot(0, this, [this]() {
            if (this->is_running() == false || this->soft_steer_pending_flag == false)
            {
                return;
            }

            const QString safe_point =
                this->run_state_machine.is_waiting_for_inline_diff_review() == true
                    ? QStringLiteral("inline_diff_review")
                    : QStringLiteral("approval_wait");
            if (this->apply_soft_steer_if_pending(safe_point) == true)
            {
                this->run_next_iteration();
            }
        });
    }

    return true;
}

void agent_controller_t::stop()
{
    if (!this->is_running())
    {
        return;
    }
    this->run_state_machine.mark_stopped();
    QCAI_INFO("Agent", QStringLiteral("Agent stopped by user at iteration %1, tool calls: %2")
                           .arg(this->current_iteration)
                           .arg(this->current_tool_call_count));
    if (this->provider != nullptr)
    {
        this->provider->cancel();
    }
    this->pending_soft_steer_messages.clear();
    this->soft_steer_pending_flag = false;
    this->pending_soft_steer_system_note.clear();
    this->disarm_provider_watchdog();
    emit this->log_message(QStringLiteral("⏹ Agent stopped by user."));
    this->finalize_persistent_run(
        QStringLiteral("stopped"),
        QJsonObject{{QStringLiteral("reason"), QStringLiteral("user-stop")}});
    emit this->stopped(
        QStringLiteral("Stopped by user at iteration %1.").arg(this->current_iteration));
}

bool agent_controller_t::persist_compaction_summary(const QString &content,
                                                    const QString &response_type)
{
    if (this->chat_context_manager == nullptr || this->run_id.isEmpty() == true)
    {
        return true;
    }

    QString context_error;
    if (this->chat_context_manager->append_compaction_summary(
            this->run_id, content,
            QJsonObject{{QStringLiteral("responseType"), response_type},
                        {QStringLiteral("mode"), QStringLiteral("compaction")}},
            &context_error) == false)
    {
        this->emit_run_log_message(
            QStringLiteral("⚠ Failed to persist compaction summary: %1").arg(context_error), true);
        return false;
    }

    return true;
}

void agent_controller_t::run_next_iteration()
{
    if (!this->is_running())
    {
        return;
    }

    if (this->apply_soft_steer_if_pending(QStringLiteral("before_provider_request")) == true)
    {
        if (this->is_running() == false)
        {
            return;
        }
    }

    // Check limits
    const int maxIter = (this->safety_policy != nullptr) ? this->safety_policy->max_iterations()
                                                         : settings().max_iterations;
    if (this->current_iteration >= maxIter)
    {
        this->run_state_machine.mark_failed();
        emit this->log_message(QStringLiteral("⚠ Max iterations reached (%1).").arg(maxIter));
        this->finalize_persistent_run(
            QStringLiteral("error"),
            QJsonObject{{QStringLiteral("reason"), QStringLiteral("max-iterations")}});
        emit this->stopped(QStringLiteral("Max iterations reached."));
        return;
    }

    ++this->current_iteration;
    if (this->run_options.is_compaction == false)
    {
        emit this->iteration_changed(this->current_iteration);
        this->emit_run_log_message(
            QStringLiteral("── Iteration %1 ──").arg(this->current_iteration));
    }

    const auto &s = settings();
    QCAI_DEBUG("Agent",
               QStringLiteral("Iteration %1: sending %2 messages to %3 (model: %4, reasoning: %5, "
                              "thinking: %6, temp: %7)")
                   .arg(this->current_iteration)
                   .arg(this->messages.size())
                   .arg(this->provider->id(), this->model_name)
                   .arg(this->reasoning_effort)
                   .arg(this->thinking_level)
                   .arg(s.temperature));

    this->run_state_machine.await_provider_response();
    this->provider_activity_seen = false;
    this->provider_request_timer.start();
    this->emit_run_log_message(QStringLiteral("⏳ Waiting for model response..."));
    if (this->pending_validation_label.isEmpty() == false)
    {
        this->handle_normalized_progress_event(
            {agent_progress_event_kind_t::VALIDATION_STARTED,
             agent_progress_operation_t::NONE,
             this->provider != nullptr ? this->provider->id() : QString(),
             QStringLiteral("controller.validation.start"),
             this->pending_validation_tool_name,
             this->pending_validation_label,
             {}});
    }
    this->arm_provider_watchdog();

    if (this->detailed_request_log != nullptr)
    {
        QList<request_log_input_part_t> input_parts;
        input_parts.reserve(this->messages.size());
        for (int i = 0; i < this->messages.size(); ++i)
        {
            const chat_message_t &message = this->messages.at(i);
            input_parts.append({QStringLiteral("message[%1] %2").arg(i).arg(message.role),
                                message.content, estimate_token_count(message.content)});
        }
        this->detailed_request_log->record_iteration_input(
            this->current_iteration, this->provider->id(), this->model_name,
            this->reasoning_effort, this->thinking_level, s.temperature, s.max_tokens,
            input_parts);
    }

    using namespace std::placeholders;
    const QPointer<agent_controller_t> controller(this);
    const iai_provider_t::completion_callback_t completion_callback =
        std::bind(&agent_controller_t::dispatch_provider_completion, controller, _1, _2, _3);
    const iai_provider_t::stream_callback_t stream_callback =
        std::bind(&agent_controller_t::dispatch_provider_stream_delta, controller, _1);
    const iai_provider_t::progress_callback_t progress_callback =
        std::bind(&agent_controller_t::dispatch_provider_progress, controller, _1);

    QList<chat_message_t> request_messages = this->messages;
    if (this->pending_soft_steer_system_note.isEmpty() == false)
    {
        int insert_index = 0;
        while (insert_index < request_messages.size() &&
               request_messages.at(insert_index).role == QStringLiteral("system"))
        {
            ++insert_index;
        }
        request_messages.insert(insert_index,
                                {QStringLiteral("system"), this->pending_soft_steer_system_note});
        this->pending_soft_steer_system_note.clear();
    }

    this->provider->complete(request_messages, this->model_name, s.temperature, s.max_tokens,
                             this->reasoning_effort, completion_callback, stream_callback,
                             progress_callback);
}

void agent_controller_t::handle_response(const QString &response, const QString &error,
                                         const provider_usage_t &usage)
{
    if (!this->is_running())
    {
        return;
    }

    this->disarm_provider_watchdog();
    const qint64 request_duration_ms = (this->provider_request_timer.isValid() == true)
                                           ? this->provider_request_timer.elapsed()
                                           : -1;

    if (this->pending_validation_label.isEmpty() == false)
    {
        this->handle_normalized_progress_event(
            {agent_progress_event_kind_t::VALIDATION_COMPLETED,
             agent_progress_operation_t::NONE,
             this->provider != nullptr ? this->provider->id() : QString(),
             QStringLiteral("controller.validation.completed"),
             this->pending_validation_tool_name,
             this->pending_validation_label,
             {}});
        this->pending_validation_tool_name.clear();
        this->pending_validation_label.clear();
    }

    if (!error.isEmpty())
    {
        if (this->detailed_request_log != nullptr)
        {
            this->detailed_request_log->record_iteration_output(
                this->current_iteration, response, error, QStringLiteral("provider_error"), {},
                usage, request_duration_ms);
        }
        this->handle_normalized_progress_event(
            {agent_progress_event_kind_t::ERROR,
             agent_progress_operation_t::NONE,
             this->provider != nullptr ? this->provider->id() : QString(),
             QStringLiteral("controller.provider.error"),
             {},
             {},
             error});
        QCAI_ERROR("Agent", QStringLiteral("Provider error: %1").arg(error));
        this->emit_run_log_message(this->run_options.is_compaction == true
                                       ? QStringLiteral("❌ Compaction failed: %1").arg(error)
                                       : QStringLiteral("❌ Provider error: %1").arg(error),
                                   true);
        emit this->error_occurred(error);
        this->run_state_machine.mark_failed();
        this->finalize_persistent_run(
            QStringLiteral("error"),
            QJsonObject{{QStringLiteral("error"), error},
                        {QStringLiteral("compaction"), this->run_options.is_compaction}});
        emit this->stopped(this->run_options.is_compaction == true
                               ? QStringLiteral("Compaction failed.")
                               : QStringLiteral("Provider error."));
        return;
    }

    if ((usage.has_any() == true) || (request_duration_ms >= 0))
    {
        this->accumulated_usage = accumulate_provider_usage(this->accumulated_usage, usage);
        emit this->provider_usage_available(usage, request_duration_ms);
    }

    if (this->soft_steer_pending_flag == true)
    {
        if (this->detailed_request_log != nullptr)
        {
            this->detailed_request_log->record_iteration_output(
                this->current_iteration, response, {}, k_superseded_by_user_steer_status, {},
                usage, request_duration_ms);
        }
        if (this->apply_soft_steer_if_pending(
                QStringLiteral("before_assistant_commit"), response,
                QJsonObject{{QStringLiteral("iteration"), this->current_iteration},
                            {QStringLiteral("reason"), k_superseded_by_user_steer_status}}) ==
            true)
        {
            this->run_next_iteration();
            return;
        }
    }

    if (this->run_options.is_compaction == true)
    {
        agent_response_t parsed = agent_response_t::parse(response);
        if (this->detailed_request_log != nullptr)
        {
            this->detailed_request_log->record_iteration_output(
                this->current_iteration, response, {}, response_type_label(parsed.type),
                parsed.summary, usage, request_duration_ms);
        }

        QString compacted_content;
        QString response_type = response_type_label(parsed.type);
        if (parsed.type == response_type_t::FINAL)
        {
            compacted_content = parsed.summary.trimmed();
        }
        else if (parsed.type == response_type_t::TEXT || parsed.type == response_type_t::ERROR)
        {
            compacted_content = parsed.text.trimmed().isEmpty() == false ? parsed.text.trimmed()
                                                                         : response.trimmed();
        }

        if (compacted_content.isEmpty() == true)
        {
            this->run_state_machine.mark_failed();
            this->emit_run_log_message(
                QStringLiteral("❌ Compaction failed: model returned an unsupported response."),
                true);
            this->finalize_persistent_run(
                QStringLiteral("error"),
                QJsonObject{
                    {QStringLiteral("reason"), QStringLiteral("compaction-invalid-response")},
                    {QStringLiteral("responseType"), response_type}});
            emit this->stopped(QStringLiteral("Compaction failed."));
            return;
        }

        if (this->persist_compaction_summary(compacted_content, response_type) == false)
        {
            this->run_state_machine.mark_failed();
            this->finalize_persistent_run(
                QStringLiteral("error"),
                QJsonObject{{QStringLiteral("reason"),
                             QStringLiteral("compaction-summary-persist-failed")},
                            {QStringLiteral("responseType"), response_type}});
            emit this->stopped(QStringLiteral("Compaction failed."));
            return;
        }

        this->run_state_machine.mark_completed();
        this->handle_normalized_progress_event(
            {agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED,
             agent_progress_operation_t::NONE,
             this->provider != nullptr ? this->provider->id() : QString(),
             QStringLiteral("controller.compaction.completed"),
             {},
             {},
             QStringLiteral("Compaction completed.")});
        this->emit_run_log_message(QStringLiteral("🗜 Compaction completed."), true);
        this->finalize_persistent_run(
            QStringLiteral("completed"),
            QJsonObject{{QStringLiteral("summary"), QStringLiteral("Compaction completed.")},
                        {QStringLiteral("responseType"), response_type},
                        {QStringLiteral("compaction"), true}});
        emit this->stopped(QStringLiteral("Compaction completed."));
        return;
    }

    // Add assistant message to history
    this->append_assistant_history_message(
        response, QStringLiteral("model_response"),
        QJsonObject{{QStringLiteral("iteration"), this->current_iteration}});

    // Parse response
    agent_response_t parsed = agent_response_t::parse(response);
    QCAI_DEBUG("Agent", QStringLiteral("Parsed response type: %1, length: %2")
                            .arg(static_cast<int>(parsed.type))
                            .arg(response.length()));
    if (this->detailed_request_log != nullptr)
    {
        this->detailed_request_log->record_iteration_output(
            this->current_iteration, response, {}, response_type_label(parsed.type),
            parsed.summary, usage, request_duration_ms);
    }

    // Reset text retry counter on valid JSON
    if (parsed.type != response_type_t::TEXT && parsed.type != response_type_t::ERROR)
    {
        this->text_retries = 0;
    }

    if (this->run_mode == run_mode_t::ASK && parsed.type != response_type_t::FINAL &&
        parsed.type != response_type_t::TEXT && parsed.type != response_type_t::ERROR)
    {
        if (this->mode_retries >= 1)
        {
            emit this->log_message(
                QStringLiteral("⚠ Ask mode received a non-final response twice."));
            this->run_state_machine.mark_failed();
            this->finalize_persistent_run(
                QStringLiteral("error"),
                QJsonObject{
                    {QStringLiteral("reason"), QStringLiteral("ask-mode-non-final-response")}});
            emit this->stopped(QStringLiteral("Ask mode received an unsupported response."));
            return;
        }

        ++this->mode_retries;
        emit this->log_message(QStringLiteral("ℹ Ask mode requested a direct final response."));
        this->append_controller_user_message(
            QStringLiteral(
                "Ask mode is enabled. Reply with one JSON object of type \"final\" only. "
                "Do not plan, call tools, or ask for approval."),
            QStringLiteral("controller_mode_correction"));
        this->run_next_iteration();
        return;
    }

    switch (parsed.type)
    {
        case response_type_t::PLAN:
            this->plan = parsed.steps;
            emit this->plan_updated(this->plan);
            emit this->log_message(
                QStringLiteral("📋 Plan with %1 step(s) received.").arg(this->plan.size()));
            // Ask model to start executing the plan
            this->append_controller_user_message(
                QStringLiteral("Good plan. Now execute step 1 using the available tools."),
                QStringLiteral("controller_plan_ack"));
            this->run_next_iteration();
            break;

        case response_type_t::TOOL_CALL:
            this->execute_tool(parsed.tool_name, parsed.tool_args);
            break;

        case response_type_t::FINAL: {
            const bool inline_refinement_enabled = settings().inline_diff_refinement_enabled;
            this->handle_normalized_progress_event(
                {agent_progress_event_kind_t::FINAL_ANSWER_STARTED,
                 agent_progress_operation_t::NONE,
                 this->provider != nullptr ? this->provider->id() : QString(),
                 QStringLiteral("controller.final.start"),
                 {},
                 {},
                 {}});
            emit this->log_message(
                QStringLiteral("%1 %2")
                    .arg((parsed.diff.isEmpty() == false && inline_refinement_enabled == true)
                             ? QStringLiteral("📝 Agent proposed changes:")
                             : QStringLiteral("✅ Agent finished:"))
                    .arg(parsed.summary));
            if (!parsed.diff.isEmpty())
            {
                // Strip "=== NEW FILE:" blocks — only show unified diff of modified files
                auto nf =
                    Diff::extract_and_create_new_files(parsed.diff, QString(), /*dryRun=*/true);
                const QString cleanDiff = nf.remaining_diff.trimmed();
                this->final_diff_preview = Diff::normalize(cleanDiff);
                if (inline_refinement_enabled == true && !this->final_diff_preview.isEmpty())
                {
                    this->run_state_machine.await_inline_diff_review();
                    this->pending_final_summary = parsed.summary;
                    emit this->diff_available(this->final_diff_preview);
                    emit this->log_message(
                        QStringLiteral("📝 Review the proposed inline diff. Resolve the hunks to "
                                       "finish, or reject changes to ask for a revised patch."));
                    break;
                }
            }
            this->pending_final_summary.clear();
            this->run_state_machine.mark_completed();
            this->handle_normalized_progress_event(
                {agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED,
                 agent_progress_operation_t::NONE,
                 this->provider != nullptr ? this->provider->id() : QString(),
                 QStringLiteral("controller.final.completed"),
                 {},
                 {},
                 parsed.summary});
            this->finalize_persistent_run(
                QStringLiteral("completed"),
                QJsonObject{{QStringLiteral("summary"), parsed.summary},
                            {QStringLiteral("responseType"), QStringLiteral("final")}});
            emit this->stopped(parsed.summary);
            break;
        }

        case response_type_t::NEED_APPROVAL: {
            pending_approval_t pa;
            pa.id = this->next_approval_id++;
            pa.tool_name = parsed.approval_action;
            this->pending_approvals.append(pa);
            this->run_state_machine.await_user_approval();
            emit this->approval_requested(pa.id, parsed.approval_action, parsed.approval_reason,
                                          parsed.approval_preview);
            emit this->log_message(
                QStringLiteral("⏸ Waiting for approval: %1").arg(parsed.approval_action));
            break;
        }

        case response_type_t::TEXT:
        case response_type_t::ERROR:
            // If model keeps responding with text, treat it as final answer
            if (this->text_retries >= 1)
            {
                this->handle_normalized_progress_event(
                    {agent_progress_event_kind_t::FINAL_ANSWER_STARTED,
                     agent_progress_operation_t::NONE,
                     this->provider != nullptr ? this->provider->id() : QString(),
                     QStringLiteral("controller.final.start"),
                     {},
                     {},
                     {}});
                if (((settings().agent_debug) == true))
                {
                    emit this->log_message(QStringLiteral("💬 %1").arg(response.left(1000)));
                }
                else
                {
                    emit this->log_message(
                        QStringLiteral("💬 Agent finished with unstructured response."));
                }
                this->run_state_machine.mark_completed();
                this->handle_normalized_progress_event(
                    {agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED,
                     agent_progress_operation_t::NONE,
                     this->provider != nullptr ? this->provider->id() : QString(),
                     QStringLiteral("controller.final.completed"),
                     {},
                     {},
                     QStringLiteral("Agent finished (text response).")});
                this->finalize_persistent_run(
                    QStringLiteral("completed"),
                    QJsonObject{{QStringLiteral("summary"),
                                 QStringLiteral("Agent finished (text response).")},
                                {QStringLiteral("responseType"), QStringLiteral("text")}});
                emit this->stopped(QStringLiteral("Agent finished (text response)."));
            }
            else
            {
                ++this->text_retries;
                emit this->log_message(
                    QStringLiteral("ℹ Raw text response, asking for JSON format."));
                this->append_controller_user_message(
                    QStringLiteral("Please respond in the required JSON format (plan, tool_call, "
                                   "final, or need_approval)."),
                    QStringLiteral("controller_json_retry"));
                this->run_next_iteration();
            }
            break;
    }
}

void agent_controller_t::execute_tool(const QString &name, const QJsonObject &args)
{
    if (!this->is_running())
    {
        return;
    }

    const int maxCalls = (this->safety_policy != nullptr) ? this->safety_policy->max_tool_calls()
                                                          : settings().max_tool_calls;
    if (this->current_tool_call_count >= maxCalls)
    {
        this->run_state_machine.mark_failed();
        emit this->log_message(QStringLiteral("⚠ Max tool calls reached (%1).").arg(maxCalls));
        this->finalize_persistent_run(
            QStringLiteral("error"),
            QJsonObject{{QStringLiteral("reason"), QStringLiteral("max-tool-calls")}});
        emit this->stopped(QStringLiteral("Max tool calls reached."));
        return;
    }

    i_tool_t *tool = (this->tool_registry != nullptr) ? this->tool_registry->tool(name) : nullptr;
    if (tool == nullptr)
    {
        this->append_controller_user_message(
            QStringLiteral("Error: unknown tool '%1'. Use one of the available tools.").arg(name),
            QStringLiteral("controller_unknown_tool"),
            QJsonObject{{QStringLiteral("tool"), name}});
        this->run_next_iteration();
        return;
    }

    // Check if approval is required
    if (tool->requires_approval() && !this->dry_run)
    {
        QString reason = (this->safety_policy != nullptr)
                             ? this->safety_policy->requires_approval(name)
                             : QStringLiteral("Tool requires approval.");

        if (!reason.isEmpty())
        {
            pending_approval_t pa;
            pa.id = this->next_approval_id++;
            pa.tool_name = name;
            pa.tool_args = args;
            this->pending_approvals.append(pa);
            this->run_state_machine.await_user_approval();
            emit this->approval_requested(
                pa.id, name, reason,
                QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Indented)));
            emit this->log_message(
                QStringLiteral("⏸ Approval required for '%1': %2").arg(name, reason));
            return;
        }
    }

    // Execute
    ++this->current_tool_call_count;
    const agent_progress_operation_t operation = classify_tool_operation(name);
    const QString progressLabel = progress_label_for_tool(name, operation);
    this->handle_normalized_progress_event(
        {agent_progress_event_kind_t::TOOL_STARTED,
         operation,
         this->provider != nullptr ? this->provider->id() : QString(),
         QStringLiteral("controller.tool.start"),
         name,
         progressLabel,
         {}});
    const bool suppressReadFileLog = (name == QStringLiteral("read_file"));
    if (!suppressReadFileLog)
    {
        emit this->log_message(format_tool_execution_log(name, args));
    }
    QCAI_DEBUG(
        "Agent",
        QStringLiteral("Tool call #%1: %2, args: %3")
            .arg(this->current_tool_call_count)
            .arg(name,
                 QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).left(200)));

    // Get workDir from editor context
    QString workDir;
    if (this->context_provider != nullptr)
    {
        auto snap = this->context_provider->capture();
        workDir = snap.project_dir;
    }

    QString result = tool->execute(args, workDir);
    if (this->detailed_request_log != nullptr)
    {
        this->detailed_request_log->record_tool_event({this->current_iteration, name, args, result,
                                                       false, false, false,
                                                       name.startsWith(QStringLiteral("mcp_"))});
    }
    if (this->chat_context_manager != nullptr)
    {
        QString contextError;
        const QJsonObject artifactMetadata{
            {QStringLiteral("tool"), name},
            {QStringLiteral("args"), args},
            {QStringLiteral("iteration"), this->current_iteration},
            {QStringLiteral("toolCallCount"), this->current_tool_call_count},
        };
        if (this->chat_context_manager->append_artifact(this->run_id, artifact_kind_for_tool(name),
                                                        name, result, artifactMetadata,
                                                        &contextError) == false &&
            contextError.isEmpty() == false)
        {
            emit this->log_message(
                QStringLiteral("⚠ Failed to persist tool output context: %1").arg(contextError));
        }
    }
    if (!suppressReadFileLog)
    {
        emit this->log_message(format_tool_result_log(result));
    }
    this->handle_normalized_progress_event(
        {agent_progress_event_kind_t::TOOL_COMPLETED,
         operation,
         this->provider != nullptr ? this->provider->id() : QString(),
         QStringLiteral("controller.tool.completed"),
         name,
         progressLabel,
         {}});

    if (this->apply_soft_steer_if_pending(QStringLiteral("after_tool_execution"), {}, {}, name,
                                          result) == true)
    {
        this->run_next_iteration();
        return;
    }

    this->pending_validation_tool_name = name;
    this->pending_validation_label = progressLabel;
    QCAI_DEBUG("Agent",
               QStringLiteral("Tool '%1' result length: %2").arg(name).arg(result.length()));

    // Feed result back to LLM
    const int maxResultLen = prompt_result_limit_for_tool(name);
    const QString truncatedResult =
        result.length() > maxResultLen
            ? result.left(maxResultLen) +
                  QStringLiteral("\n... [truncated, %1 chars total]").arg(result.length())
            : result;
    this->append_controller_user_message(
        QStringLiteral("Tool '%1' returned:\n%2\n\nContinue with the next step.")
            .arg(name, truncatedResult),
        QStringLiteral("tool_result"),
        QJsonObject{{QStringLiteral("tool"), name},
                    {QStringLiteral("truncated"), result.length() > maxResultLen}});
    this->run_next_iteration();
}

void agent_controller_t::arm_provider_watchdog()
{
    if (this->is_running() && this->run_state_machine.is_waiting_for_provider())
    {
        const bool thinkingEnabled = !this->thinking_level.isEmpty() &&
                                     this->thinking_level != QStringLiteral("off") &&
                                     this->thinking_level != QStringLiteral("none");
        const int timeoutMs = thinkingEnabled ? k_provider_thinking_inactivity_timeout_ms
                                              : k_provider_inactivity_timeout_ms;
        this->provider_watchdog.setInterval(timeoutMs);
        this->provider_watchdog.start();
    }
}

void agent_controller_t::disarm_provider_watchdog()
{
    this->provider_watchdog.stop();
    this->provider_activity_seen = false;
}

void agent_controller_t::handle_provider_inactivity_timeout()
{
    if (!this->is_running() || !this->run_state_machine.is_waiting_for_provider())
    {
        return;
    }

    const int actualTimeoutMs = this->provider_watchdog.interval();
    QCAI_ERROR("Agent", QStringLiteral("Provider response timed out after %1 ms of inactivity")
                            .arg(actualTimeoutMs));
    this->handle_normalized_progress_event(
        {agent_progress_event_kind_t::ERROR,
         agent_progress_operation_t::NONE,
         this->provider != nullptr ? this->provider->id() : QString(),
         QStringLiteral("controller.provider.timeout"),
         {},
         {},
         QStringLiteral("Provider response timed out.")});
    if (this->provider != nullptr)
    {
        this->provider->cancel();
    }

    emit this->log_message(
        QStringLiteral("⌛ Provider response timed out after %1 seconds of inactivity.")
            .arg(actualTimeoutMs / 1000));
    emit this->error_occurred(QStringLiteral("Provider response timed out."));
    this->run_state_machine.mark_failed();
    this->disarm_provider_watchdog();
    this->finalize_persistent_run(
        QStringLiteral("error"),
        QJsonObject{{QStringLiteral("reason"), QStringLiteral("provider-timeout")}});
    emit this->stopped(QStringLiteral("Provider response timed out."));
}

bool agent_controller_t::apply_soft_steer_if_pending(const QString &safe_point,
                                                     const QString &superseded_assistant_output,
                                                     const QJsonObject &superseded_metadata,
                                                     const QString &completed_tool_name,
                                                     const QString &completed_tool_result)
{
    if (this->soft_steer_pending_flag == false ||
        this->pending_soft_steer_messages.isEmpty() == true)
    {
        return false;
    }

    const int queued_message_count = static_cast<int>(this->pending_soft_steer_messages.size());
    emit this->status_changed(QStringLiteral("Applying follow-up user steer..."));

    if (superseded_assistant_output.trimmed().isEmpty() == false)
    {
        QJsonObject metadata = superseded_metadata;
        metadata[QStringLiteral("status")] = k_superseded_by_user_steer_status;
        metadata[QStringLiteral("safePoint")] = safe_point;
        metadata[QStringLiteral("queuedSteerMessages")] = queued_message_count;
        this->append_assistant_history_message(superseded_assistant_output,
                                               k_superseded_by_user_steer_status, metadata, false);
    }

    if (completed_tool_name.isEmpty() == false)
    {
        this->append_controller_user_message(
            soft_steer_tool_completion_note(completed_tool_name, completed_tool_result),
            QStringLiteral("soft_steer_tool_completion"),
            QJsonObject{{QStringLiteral("tool"), completed_tool_name},
                        {QStringLiteral("safePoint"), safe_point},
                        {QStringLiteral("softSteer"), true}});
    }

    this->pending_validation_tool_name.clear();
    this->pending_validation_label.clear();
    this->pending_approvals.clear();
    this->pending_final_summary.clear();
    this->final_diff_preview.clear();
    emit this->diff_available(QString());

    this->pending_soft_steer_system_note = soft_steer_system_note();
    QStringList soft_steer_request_logs;

    for (int i = 0; i < this->pending_soft_steer_messages.size(); ++i)
    {
        const pending_soft_steer_message_t &queued = this->pending_soft_steer_messages.at(i);
        soft_steer_request_logs.append(format_soft_steer_request_log(
            queued.content, queued.request_context, queued.linked_files, i, queued_message_count));
        this->append_controller_user_message(
            soft_steer_follow_up_message(queued.content, queued.request_context),
            QStringLiteral("soft_steer_followup"),
            QJsonObject{
                {QStringLiteral("softSteer"), true},
                {QStringLiteral("status"), QStringLiteral("applied")},
                {QStringLiteral("safePoint"), safe_point},
                {QStringLiteral("steerIndex"), i},
                {QStringLiteral("queuedMessageCount"), queued_message_count},
                {QStringLiteral("linkedFiles"), QJsonArray::fromStringList(queued.linked_files)}});
    }

    this->pending_soft_steer_messages.clear();
    this->soft_steer_pending_flag = false;
    this->provider_activity_seen = false;

    emit this->soft_steer_applied(queued_message_count);
    for (const QString &request_log : std::as_const(soft_steer_request_logs))
    {
        emit this->log_message(request_log);
    }
    QCAI_INFO("Agent",
              QStringLiteral("Applied soft steer at safe point '%1' with %2 queued follow-up "
                             "message(s)")
                  .arg(safe_point)
                  .arg(queued_message_count));
    return true;
}

void agent_controller_t::request_inline_diff_refinement(const QString &accepted_diff,
                                                        const QString &rejected_diff)
{
    if (!this->is_running() || !this->run_state_machine.is_waiting_for_inline_diff_review())
    {
        return;
    }

    const QString normalized_rejected = Diff::normalize(rejected_diff).trimmed();
    if (normalized_rejected.isEmpty() == true)
    {
        this->finalize_inline_diff_review();
        return;
    }

    this->accepted_inline_diff_preview =
        merge_diff_sections(this->accepted_inline_diff_preview, accepted_diff);
    this->pending_final_summary.clear();
    this->final_diff_preview.clear();

    emit this->diff_available(QString());
    emit this->log_message(
        QStringLiteral("↩️ Inline diff feedback received. Asking the agent for a revised patch."));

    QString feedback =
        QStringLiteral("The user reviewed your proposed inline diff and wants a different "
                       "implementation for the rejected changes.\n\n");
    if (this->accepted_inline_diff_preview.isEmpty() == false)
    {
        feedback += QStringLiteral("These accepted hunks are already applied to the workspace and "
                                   "must be preserved exactly:\n%1\n\n")
                        .arg(as_indented_code_block(this->accepted_inline_diff_preview));
    }
    feedback += QStringLiteral("Replace the following rejected hunks with a different approach:\n"
                               "%1\n\n"
                               "Reply with one JSON object of type \"final\" only. Keep the "
                               "summary concise and include a unified diff that covers only the "
                               "replacement changes still needed.")
                    .arg(as_indented_code_block(normalized_rejected));

    this->append_controller_user_message(
        feedback, QStringLiteral("inline_diff_rejected"),
        QJsonObject{
            {QStringLiteral("acceptedDiff"), this->accepted_inline_diff_preview.left(4000)},
            {QStringLiteral("rejectedDiff"), normalized_rejected.left(4000)}});
    this->run_next_iteration();
}

void agent_controller_t::finalize_inline_diff_review()
{
    if (!this->is_running() || !this->run_state_machine.is_waiting_for_inline_diff_review())
    {
        return;
    }

    this->run_state_machine.mark_completed();
    const QString summary = this->pending_final_summary.trimmed().isEmpty() == false
                                ? this->pending_final_summary.trimmed()
                                : QStringLiteral("Inline diff review completed.");
    this->pending_final_summary.clear();
    this->handle_normalized_progress_event(
        {agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED,
         agent_progress_operation_t::NONE,
         this->provider != nullptr ? this->provider->id() : QString(),
         QStringLiteral("controller.final.completed"),
         {},
         {},
         summary});
    this->finalize_persistent_run(
        QStringLiteral("completed"),
        QJsonObject{{QStringLiteral("summary"), summary},
                    {QStringLiteral("responseType"), QStringLiteral("final")},
                    {QStringLiteral("completedAfterInlineReview"), true}});
    emit this->stopped(summary);
}

void agent_controller_t::append_controller_user_message(const QString &content,
                                                        const QString &source,
                                                        const QJsonObject &metadata)
{
    this->messages.append({QStringLiteral("user"), content});
    if (this->chat_context_manager != nullptr)
    {
        QString contextError;
        if (this->chat_context_manager->append_user_message(this->run_id, content, source,
                                                            metadata, &contextError) == false &&
            contextError.isEmpty() == false)
        {
            emit this->log_message(
                QStringLiteral("⚠ Failed to persist chat history: %1").arg(contextError));
        }
    }
}

void agent_controller_t::append_assistant_history_message(const QString &content,
                                                          const QString &source,
                                                          const QJsonObject &metadata,
                                                          bool include_in_prompt)
{
    if (include_in_prompt == true)
    {
        this->messages.append({QStringLiteral("assistant"), content});
    }
    if (this->chat_context_manager != nullptr)
    {
        QString contextError;
        if (this->chat_context_manager->append_assistant_message(
                this->run_id, content, source, metadata, &contextError) == false &&
            contextError.isEmpty() == false)
        {
            emit this->log_message(
                QStringLiteral("⚠ Failed to persist assistant history: %1").arg(contextError));
        }
    }
}

void agent_controller_t::finalize_persistent_run(const QString &status,
                                                 const QJsonObject &metadata)
{
    this->finalize_detailed_request_log(status, metadata);

    if (this->chat_context_manager == nullptr || this->run_id.isEmpty() == true)
    {
        return;
    }

    QString artifactError;
    const QString persisted_diff =
        merge_diff_sections(this->accepted_inline_diff_preview, this->final_diff_preview);
    if (persisted_diff.isEmpty() == false)
    {
        this->chat_context_manager->append_artifact(
            this->run_id, QStringLiteral("diff"), QStringLiteral("final_diff"), persisted_diff,
            QJsonObject{{QStringLiteral("status"), status}}, &artifactError);
    }
    QString finishError;
    this->chat_context_manager->finish_run(this->run_id, status, this->accumulated_usage, metadata,
                                           &finishError);

    if (artifactError.isEmpty() == false)
    {
        emit this->log_message(
            QStringLiteral("⚠ Failed to persist final diff artifact: %1").arg(artifactError));
    }
    if (finishError.isEmpty() == false)
    {
        emit this->log_message(
            QStringLiteral("⚠ Failed to finalize persistent chat context: %1").arg(finishError));
    }

    this->run_id.clear();
}

void agent_controller_t::finalize_detailed_request_log(const QString &status,
                                                       const QJsonObject &metadata)
{
    if (this->detailed_request_log == nullptr)
    {
        return;
    }

    this->detailed_request_log->finish_request(
        status, metadata, this->accumulated_usage,
        merge_diff_sections(this->accepted_inline_diff_preview, this->final_diff_preview));
    QString log_error;
    if (this->detailed_request_log->append_to_project_log(&log_error) == false &&
        log_error.isEmpty() == false)
    {
        emit this->log_message(
            QStringLiteral("⚠ Failed to write detailed request log: %1").arg(log_error));
    }
    this->detailed_request_log.reset();
}

void agent_controller_t::dispatch_provider_completion(QPointer<agent_controller_t> controller,
                                                      const QString &response,
                                                      const QString &error,
                                                      const provider_usage_t &usage)
{
    if (controller.isNull() == true)
    {
        return;
    }

    controller->enqueue_provider_response(response, error, usage);
}

void agent_controller_t::dispatch_provider_stream_delta(QPointer<agent_controller_t> controller,
                                                        const QString &delta)
{
    if (controller.isNull() == true)
    {
        return;
    }

    controller->handle_provider_stream_delta(delta);
}

void agent_controller_t::dispatch_provider_progress(QPointer<agent_controller_t> controller,
                                                    const provider_raw_event_t &event)
{
    if (controller.isNull() == true)
    {
        return;
    }

    controller->handle_provider_progress_event(event);
}

void agent_controller_t::enqueue_provider_response(const QString &response, const QString &error,
                                                   const provider_usage_t &usage)
{
    this->pending_provider_responses.append({response, error, usage});
    if (this->provider_response_dispatch_scheduled == true)
    {
        return;
    }

    this->provider_response_dispatch_scheduled = true;
    QTimer::singleShot(0, this, &agent_controller_t::drain_queued_provider_responses);
}

void agent_controller_t::drain_queued_provider_responses()
{
    this->provider_response_dispatch_scheduled = false;

    while (this->pending_provider_responses.isEmpty() == false)
    {
        const pending_provider_response_t pending = this->pending_provider_responses.takeFirst();
        this->handle_response(pending.response, pending.error, pending.usage);
    }
}

void agent_controller_t::handle_provider_stream_delta(const QString &delta)
{
    if (delta.isEmpty() == true)
    {
        return;
    }

    if (this->provider_activity_seen == false)
    {
        this->provider_activity_seen = true;
        this->emit_run_log_message(QStringLiteral("✍ Model response in progress..."));
    }
    this->arm_provider_watchdog();
    if (this->run_options.is_compaction == false)
    {
        emit this->streaming_token(delta);
    }
}

void agent_controller_t::handle_provider_progress_event(const provider_raw_event_t &event)
{
    if (this->progress_tracker == nullptr)
    {
        return;
    }

    this->apply_progress_render_result(this->progress_tracker->handle_provider_raw_event(event));
}

void agent_controller_t::handle_normalized_progress_event(const agent_progress_event_t &event)
{
    if (this->progress_tracker == nullptr)
    {
        return;
    }

    this->apply_progress_render_result(this->progress_tracker->handle_normalized_event(event));
}

void agent_controller_t::apply_progress_render_result(const agent_progress_render_result_t &result)
{
    if (result.status_changed == true && result.status_text.isEmpty() == false)
    {
        emit this->status_changed(result.status_text);
    }
    if (result.stable_log_line.isEmpty() == false)
    {
        emit this->log_message(result.stable_log_line);
    }
    for (const QString &line : result.debug_lines)
    {
        QCAI_DEBUG("Progress", line);
    }
}

void agent_controller_t::approve_action(int approvalId)
{
    for (int i = 0; i < this->pending_approvals.size(); ++i)
    {
        if (((this->pending_approvals[i].id == approvalId) == true))
        {
            auto pa = this->pending_approvals.takeAt(i);
            emit this->log_message(QStringLiteral("✅ Approved: %1").arg(pa.tool_name));

            if (!pa.tool_name.isEmpty() && (this->tool_registry != nullptr))
            {
                i_tool_t *tool = this->tool_registry->tool(pa.tool_name);
                if (tool != nullptr)
                {
                    ++this->current_tool_call_count;
                    QString workDir;
                    if (this->context_provider != nullptr)
                    {
                        workDir = this->context_provider->capture().project_dir;
                    }
                    QString result = tool->execute(pa.tool_args, workDir);
                    if (this->detailed_request_log != nullptr)
                    {
                        this->detailed_request_log->record_tool_event(
                            {this->current_iteration, pa.tool_name, pa.tool_args, result, true,
                             true, false, pa.tool_name.startsWith(QStringLiteral("mcp_"))});
                    }
                    if (this->chat_context_manager != nullptr)
                    {
                        QString contextError;
                        const QJsonObject artifactMetadata{
                            {QStringLiteral("tool"), pa.tool_name},
                            {QStringLiteral("args"), pa.tool_args},
                            {QStringLiteral("approved"), true},
                        };
                        if (this->chat_context_manager->append_artifact(
                                this->run_id, artifact_kind_for_tool(pa.tool_name), pa.tool_name,
                                result, artifactMetadata, &contextError) == false &&
                            contextError.isEmpty() == false)
                        {
                            emit this->log_message(
                                QStringLiteral("⚠ Failed to persist approved tool output: %1")
                                    .arg(contextError));
                        }
                    }
                    this->append_controller_user_message(
                        QStringLiteral("Approved and executed '%1'. result_t:\n%2\n\nContinue.")
                            .arg(pa.tool_name, result),
                        QStringLiteral("approved_tool_result"),
                        QJsonObject{{QStringLiteral("tool"), pa.tool_name}});
                }
            }
            this->run_next_iteration();
            return;
        }
    }
}

void agent_controller_t::deny_action(int approvalId)
{
    for (int i = 0; i < this->pending_approvals.size(); ++i)
    {
        if (((this->pending_approvals[i].id == approvalId) == true))
        {
            auto pa = this->pending_approvals.takeAt(i);
            emit this->log_message(QStringLiteral("❌ Denied: %1").arg(pa.tool_name));
            if (this->detailed_request_log != nullptr)
            {
                this->detailed_request_log->record_tool_event(
                    {this->current_iteration,
                     pa.tool_name,
                     pa.tool_args,
                     {},
                     true,
                     false,
                     true,
                     pa.tool_name.startsWith(QStringLiteral("mcp_"))});
            }
            this->append_controller_user_message(
                QStringLiteral("The user denied the action '%1'. Find an alternative approach or "
                               "finish.")
                    .arg(pa.tool_name),
                QStringLiteral("approval_denied"),
                QJsonObject{{QStringLiteral("tool"), pa.tool_name}});
            this->run_next_iteration();
            return;
        }
    }
}

}  // namespace qcai2
