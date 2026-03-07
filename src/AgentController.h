/*! Declares the core plan-act-observe controller that drives an agent session. */
#pragma once

#include "context/EditorContext.h"
#include "models/AgentMessages.h"
#include "providers/IAIProvider.h"
#include "safety/SafetyPolicy.h"
#include "tools/ToolRegistry.h"

#include <QList>
#include <QObject>
#include <QString>

namespace qcai2
{

/**
 * Coordinates one agent run from planning through tool execution and completion.
 */
class AgentController : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a controller with no configured provider or tools.
     * @param parent Parent QObject that owns this instance.
     */
    explicit AgentController(QObject *parent = nullptr);

    /**
     * Sets the provider used when no dynamic provider selection is needed.
     * @param provider Provider instance to use.
     */
    void setProvider(IAIProvider *provider);
    /**
     * Sets the provider list used to reselect the active backend at start time.
     * @param providers Provider instances available for selection.
     */
    void setProviders(const QList<IAIProvider *> &providers);
    /**
     * Sets the registry of tools that may be executed by the model.
     * @param registry Tool registry exposed to the agent.
     */
    void setToolRegistry(ToolRegistry *registry);
    /**
     * Sets the editor context used to build prompts and locate the project.
     * @param ctx Editor context used for prompt generation.
     */
    void setEditorContext(EditorContext *ctx);
    /**
     * Sets the safety policy that caps iterations, tool calls, and approvals.
     * @param policy Safety policy used to gate agent actions.
     */
    void setSafetyPolicy(SafetyPolicy *policy);

    /**
     * Starts a new run for the requested goal.
     * @param goal User task description.
     * @param dryRun True to forbid live patch application.
     */
    void start(const QString &goal, bool dryRun);

    /**
     * Stops the active run and cancels any in-flight provider request.
     */
    void stop();

    /**
     * Executes a previously approved tool request.
     * @param approvalId Pending approval id.
     */
    void approveAction(int approvalId);

    /**
     * Rejects a pending tool request and asks the model to adapt.
     * @param approvalId Pending approval id.
     */
    void denyAction(int approvalId);

    /**
     * Returns true while a run is active.
     */
    bool isRunning() const
    {
        return m_running;
    }
    /**
     * Returns the completed iteration count for the active run.
     */
    int iteration() const
    {
        return m_iteration;
    }
    /**
     * Returns how many tools have executed in the active run.
     */
    int toolCallCount() const
    {
        return m_toolCallCount;
    }
    /**
     * Returns the editor context currently used for prompt generation.
     */
    EditorContext *editorContext() const
    {
        return m_editorContext;
    }

    /**
     * Returns the final diff preview accumulated for the UI.
     */
    QString accumulatedDiff() const
    {
        return m_accumulatedDiff;
    }
    /**
     * Returns the most recent plan supplied by the model.
     */
    QList<PlanStep> currentPlan() const
    {
        return m_plan;
    }

signals:
    /**
     * Emits user-visible log output for the dock widget.
     * @param msg Log message text.
     */
    void logMessage(const QString &msg);
    /**
     * Streams partial provider output while a response is in flight.
     * @param token Streamed token text.
     */
    void streamingToken(const QString &token);
    /**
     * Publishes the latest multi-step plan returned by the model.
     * @param steps Plan steps to display.
     */
    void planUpdated(const QList<PlanStep> &steps);
    /**
     * Publishes the final unified diff preview, if any.
     * @param diff Unified diff text.
     */
    void diffAvailable(const QString &diff);
    /**
     * Requests user approval before executing a guarded tool in live mode.
     * @param id Identifier value.
     * @param action Action description shown to the user.
     * @param reason Reason the approval is required.
     * @param preview Preview text shown with the approval request.
     */
    void approvalRequested(int id, const QString &action, const QString &reason,
                           const QString &preview);
    /**
     * Reports the 1-based iteration count after it increments.
     * @param iteration Current iteration number.
     */
    void iterationChanged(int iteration);
    /**
     * Signals that the run finished or was stopped, with a short summary.
     * @param summary Short completion summary.
     */
    void stopped(const QString &summary);
    /**
     * Signals a provider or controller error that ended the run.
     * @param error Error text.
     */
    void errorOccurred(const QString &error);

private:
    /**
     * Sends the current conversation to the provider for the next step.
     */
    void runNextIteration();
    /**
     * Parses one provider response and dispatches the next controller action.
     * @param response Provider response text.
     * @param error Error text.
     */
    void handleResponse(const QString &response, const QString &error);
    /**
     * Runs a tool call, handling approvals, limits, and result truncation.
     * @param name Name value.
     * @param args Tool arguments.
     */
    void executeTool(const QString &name, const QJsonObject &args);
    /**
     * Builds the system prompt from tools, editor context, and safety mode.
     */
    QString buildSystemPrompt() const;

    /** Active provider used for the current run. */
    IAIProvider *m_provider = nullptr;
    /** All configured providers available for reselection at start time. */
    QList<IAIProvider *> m_allProviders;
    /** Registered tools exposed to the model. */
    ToolRegistry *m_toolRegistry = nullptr;
    /** Editor and project snapshot provider. */
    EditorContext *m_editorContext = nullptr;
    /** Limits for iterations, tool calls, and approval handling. */
    SafetyPolicy *m_safetyPolicy = nullptr;

    /** True while the controller is processing a run. */
    bool m_running = false;
    /** True when live patch application is disabled. */
    bool m_dryRun = true;
    /** Completed iteration count for the active run. */
    int m_iteration = 0;
    /** Executed tool call count for the active run. */
    int m_toolCallCount = 0;
    /** Consecutive unstructured responses seen without valid JSON. */
    int m_textRetries = 0;

    /** User goal for the current run. */
    QString m_goal;
    /** Conversation history replayed to the provider every iteration. */
    QList<ChatMessage> m_messages;
    /** Most recent multi-step plan returned by the model. */
    QList<PlanStep> m_plan;
    /** Final diff preview accumulated across responses. */
    QString m_accumulatedDiff;

    /** Pending tool execution waiting for an explicit user decision. */
    struct PendingApproval
    {
        /** Stable identifier emitted to the UI. */
        int id;
        /** Tool name requested by the model. */
        QString toolName;
        /** Tool arguments to replay after approval. */
        QJsonObject toolArgs;
    };
    /** Queue of tool calls waiting for user approval. */
    QList<PendingApproval> m_pendingApprovals;
    /** Next approval identifier emitted to the UI. */
    int m_nextApprovalId = 1;
};

}  // namespace qcai2
