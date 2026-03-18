/*! Declares the core plan-act-observe controller that drives an agent session. */
#include <cstdint>
#pragma once

#include "context/editor_context.h"
#include "models/agent_messages.h"
#include "progress/agent_progress.h"
#include "progress/agent_progress_tracker.h"
#include "providers/iai_provider.h"
#include "safety/safety_policy.h"
#include "tools/tool_registry.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace qcai2
{

class mcp_tool_manager_t;
class chat_context_manager_t;

/**
 * Coordinates one agent run from planning through tool execution and completion.
 */
class agent_controller_t : public QObject
{
    Q_OBJECT
public:
    /**
     * High-level run mode selected in the dock widget.
     */
    enum class run_mode_t : std::uint8_t
    {
        ASK,
        AGENT
    };

    /**
     * Creates a controller with no configured provider or tools.
     * @param parent Parent QObject that owns this instance.
     */
    explicit agent_controller_t(QObject *parent = nullptr);

    /**
     * Sets the provider used when no dynamic provider selection is needed.
     * @param provider Provider instance to use.
     */
    void set_provider(iai_provider_t *provider);

    /**
     * Sets the provider list used to reselect the active backend at start time.
     * @param providers Provider instances available for selection.
     */
    void set_providers(const QList<iai_provider_t *> &providers);

    /**
     * Sets the registry of tools that may be executed by the model.
     * @param registry Tool registry exposed to the agent.
     */
    void set_tool_registry(tool_registry_t *registry);

    /**
     * Sets the editor context used to build prompts and locate the project.
     * @param ctx Editor context used for prompt generation.
     */
    void set_editor_context(editor_context_t *ctx);

    /**
     * Sets the persistent chat context manager used for long-lived history.
     * @param manager Shared context manager instance.
     */
    void set_chat_context_manager(chat_context_manager_t *manager);

    /**
     * Sets the MCP runtime bridge used to expose configured MCP tools.
     * @param manager Runtime MCP tool manager.
     */
    void set_mcp_tool_manager(mcp_tool_manager_t *manager);

    /**
     * Sets the safety policy that caps iterations, tool calls, and approvals.
     * @param policy Safety policy used to gate agent actions.
     */
    void set_safety_policy(safety_policy_t *policy);

    /**
     * Sets extra request-specific context, such as linked files, for the next run.
     * @param context Additional prompt context supplied alongside the goal.
     * @param linked_files Display names of linked files for logging.
     */
    void set_request_context(const QString &context, const QStringList &linked_files = {});

    /**
     * Starts a new run for the requested goal.
     * @param goal User task description.
     * @param dry_run True to forbid live patch application.
     * @param run_mode Ask for direct answers or Agent for autonomous tool use.
     * @param model_name Model selected in the dock widget.
     * @param reasoning_effort Reasoning effort selected in the dock widget.
     * @param thinking_level Thinking depth selected in the dock widget.
     */
    void start(const QString &goal, bool dry_run, run_mode_t run_mode, const QString &model_name,
               const QString &reasoning_effort, const QString &thinking_level);

    /**
     * Stops the active run and cancels any in-flight provider request.
     */
    void stop();

    /**
     * Executes a previously approved tool request.
     * @param approvalId Pending approval id.
     */
    void approve_action(int approval_id);

    /**
     * Rejects a pending tool request and asks the model to adapt.
     * @param approvalId Pending approval id.
     */
    void deny_action(int approval_id);

    /**
     * Returns true while a run is active.
     */
    bool is_running() const
    {
        return this->running;
    }

    /**
     * Returns the completed iteration count for the active run.
     */
    int iteration() const
    {
        return this->current_iteration;
    }

    /**
     * Returns how many tools have executed in the active run.
     */
    int tool_call_count() const
    {
        return this->current_tool_call_count;
    }

    /**
     * Returns the editor context currently used for prompt generation.
     */
    editor_context_t *editor_context() const
    {
        return this->context_provider;
    }

    /**
     * Returns the final diff preview accumulated for the UI.
     */
    QString accumulated_diff() const
    {
        return this->final_diff_preview;
    }

    /**
     * Returns the most recent plan supplied by the model.
     */
    QList<plan_step_t> current_plan() const
    {
        return this->plan;
    }

signals:
    /**
     * Emits user-visible log output for the dock widget.
     * @param msg Log message text.
     */
    void log_message(const QString &msg);

    /**
     * Streams partial provider output while a response is in flight.
     * @param token Streamed token text.
     */
    void streaming_token(const QString &token);

    /**
     * Publishes provider token-usage counters for one completed model request.
     * @param usage Usage counters returned by the provider.
     * @param durationMs Wall-clock duration of the completed request in milliseconds.
     */
    void provider_usage_available(const provider_usage_t &usage, qint64 duration_ms);

    /**
     * Publishes the latest human-friendly live progress status.
     * @param status User-visible status text.
     */
    void status_changed(const QString &status);

    /**
     * Publishes the latest multi-step plan returned by the model.
     * @param steps Plan steps to display.
     */
    void plan_updated(const QList<plan_step_t> &steps);

    /**
     * Publishes the final unified diff preview, if any.
     * @param diff Unified diff text.
     */
    void diff_available(const QString &diff);

    /**
     * Requests user approval before executing a guarded tool in live mode.
     * @param id Identifier value.
     * @param action Action description shown to the user.
     * @param reason Reason the approval is required.
     * @param preview Preview text shown with the approval request.
     */
    void approval_requested(int id, const QString &action, const QString &reason,
                            const QString &preview);

    /**
     * Reports the 1-based iteration count after it increments.
     * @param iteration Current iteration number.
     */
    void iteration_changed(int iteration);

    /**
     * Signals that the run finished or was stopped, with a short summary.
     * @param summary Short completion summary.
     */
    void stopped(const QString &summary);

    /**
     * Signals a provider or controller error that ended the run.
     * @param error Error text.
     */
    void error_occurred(const QString &error);

private:
    /**
     * Sends the current conversation to the provider for the next step.
     */
    void run_next_iteration();

    /**
     * Parses one provider response and dispatches the next controller action.
     * @param response Provider response text.
     * @param error Error text.
     * @param usage Provider token-usage counters when available.
     */
    void handle_response(const QString &response, const QString &error,
                         const provider_usage_t &usage);

    /**
     * Runs a tool call, handling approvals, limits, and result truncation.
     * @param name Name value.
     * @param args Tool arguments.
     */
    void execute_tool(const QString &name, const QJsonObject &args);

    /**
     * Builds the system prompt from tools, editor context, and safety mode.
     */
    QString build_system_prompt() const;

    /**
     * Arms the inactivity watchdog for an in-flight provider request.
     */
    void arm_provider_watchdog();

    /**
     * Stops the inactivity watchdog after a provider request completes.
     */
    void disarm_provider_watchdog();

    /**
     * Aborts the current run when the provider stays inactive for too long.
     */
    void handle_provider_inactivity_timeout();

    /**
     * Persists one controller-authored user message in both prompt state and chat history.
     */
    void append_controller_user_message(const QString &content, const QString &source,
                                        const QJsonObject &metadata = {});

    /**
     * Persists one assistant response in both prompt state and chat history.
     */
    void append_assistant_history_message(const QString &content, const QString &source,
                                          const QJsonObject &metadata = {});

    /**
     * Finalizes the current persistent run record, if any.
     */
    void finalize_persistent_run(const QString &status, const QJsonObject &metadata = {});

    /**
     * Routes one raw provider progress event through the tracker.
     */
    void handle_provider_progress_event(const provider_raw_event_t &event);

    /**
     * Routes one controller-generated normalized progress event through the tracker.
     */
    void handle_normalized_progress_event(const agent_progress_event_t &event);

    /**
     * Applies one tracker render result to UI-facing signals and debug logs.
     */
    void apply_progress_render_result(const agent_progress_render_result_t &result);

    /**
     * Dispatches one provider completion callback safely back to the controller.
     */
    static void dispatch_provider_completion(QPointer<agent_controller_t> controller,
                                             const QString &response, const QString &error,
                                             const provider_usage_t &usage);

    /**
     * Dispatches one provider stream delta safely back to the controller.
     */
    static void dispatch_provider_stream_delta(QPointer<agent_controller_t> controller,
                                               const QString &delta);

    /**
     * Dispatches one provider progress event safely back to the controller.
     */
    static void dispatch_provider_progress(QPointer<agent_controller_t> controller,
                                           const provider_raw_event_t &event);

    /**
     * Queues one provider response for deferred processing on the next event-loop turn.
     */
    void enqueue_provider_response(const QString &response, const QString &error,
                                   const provider_usage_t &usage);

    /**
     * Drains all deferred provider responses.
     */
    void drain_queued_provider_responses();

    /**
     * Processes one streamed provider delta.
     */
    void handle_provider_stream_delta(const QString &delta);

    /** Active provider used for the current run. */
    iai_provider_t *provider = nullptr;

    /** All configured providers available for reselection at start time. */
    QList<iai_provider_t *> all_providers;

    /** Registered tools exposed to the model. */
    tool_registry_t *tool_registry = nullptr;

    /** Editor and project snapshot provider. */
    editor_context_t *context_provider = nullptr;

    /** Persistent chat context manager shared across runs and workspaces. */
    chat_context_manager_t *chat_context_manager = nullptr;

    /** Runtime bridge that injects configured MCP tools before agent runs. */
    mcp_tool_manager_t *mcp_tool_manager = nullptr;

    /** Limits for iterations, tool calls, and approval handling. */
    safety_policy_t *safety_policy = nullptr;

    /** True while the controller is processing a run. */
    bool running = false;

    /** True when live patch application is disabled. */
    bool dry_run = true;

    /** Completed iteration count for the active run. */
    int current_iteration = 0;

    /** Executed tool call count for the active run. */
    int current_tool_call_count = 0;

    /** Consecutive unstructured responses seen without valid JSON. */
    int text_retries = 0;

    /** User goal for the current run. */
    QString goal;

    /** Extra request-specific prompt context prepared by the dock widget. */
    QString request_context;

    /** Linked file labels associated with the pending request. */
    QStringList linked_files;

    /** Selected interaction mode for the current run. */
    run_mode_t run_mode = run_mode_t::AGENT;

    /** Selected model for the current run. */
    QString model_name;

    /** Selected reasoning effort for the current run. */
    QString reasoning_effort;

    /** Selected thinking depth for the current run. */
    QString thinking_level;

    /** Ask-mode retries after the model returns an autonomous response. */
    int mode_retries = 0;

    /** Conversation history replayed to the provider every iteration. */
    QList<chat_message_t> messages;

    /** Persistent run id backing the current controller run. */
    QString run_id;

    /** Accumulated provider usage across all model requests in the current run. */
    provider_usage_t accumulated_usage;

    /** Tracker that normalizes provider/controller activity into user-facing statuses. */
    std::unique_ptr<agent_progress_tracker_t> progress_tracker;

    /** Provider responses deferred to avoid re-entrancy while callbacks are in flight. */
    struct pending_provider_response_t
    {
        QString response;
        QString error;
        provider_usage_t usage;
    };

    /** Deferred provider responses waiting for queued handling. */
    QList<pending_provider_response_t> pending_provider_responses;

    /** True while a deferred provider-response drain is already scheduled. */
    bool provider_response_dispatch_scheduled = false;

    /** Tool currently awaiting model-side validation after it completed. */
    QString pending_validation_tool_name;

    /** Human-friendly label for the tool currently awaiting validation. */
    QString pending_validation_label;

    /** Measures elapsed time for the in-flight provider request. */
    QElapsedTimer provider_request_timer;

    /** Most recent multi-step plan returned by the model. */
    QList<plan_step_t> plan;

    /** Final diff preview accumulated across responses. */
    QString final_diff_preview;

    /** Pending tool execution waiting for an explicit user decision. */
    struct pending_approval_t
    {
        /** Stable identifier emitted to the UI. */
        int id;

        /** Tool name requested by the model. */
        QString tool_name;

        /** Tool arguments to replay after approval. */
        QJsonObject tool_args;
    };
    /** Queue of tool calls waiting for user approval. */
    QList<pending_approval_t> pending_approvals;

    /** Next approval identifier emitted to the UI. */
    int next_approval_id = 1;

    /** Kills agent runs that stall waiting for a provider response. */
    QTimer provider_watchdog;

    /** True while a provider request is currently in flight. */
    bool waiting_for_provider = false;

    /** Tracks whether the current request has produced any visible activity yet. */
    bool provider_activity_seen = false;
};

}  // namespace qcai2
