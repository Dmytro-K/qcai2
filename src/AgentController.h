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

// The autonomous agent loop controller.
// Plan → act (tools) → observe → verify → iterate.
class AgentController : public QObject
{
    Q_OBJECT
public:
    explicit AgentController(QObject *parent = nullptr);

    void setProvider(IAIProvider *provider);
    void setProviders(const QList<IAIProvider *> &providers);
    void setToolRegistry(ToolRegistry *registry);
    void setEditorContext(EditorContext *ctx);
    void setSafetyPolicy(SafetyPolicy *policy);

    // Start the agent loop with a user goal.
    void start(const QString &goal, bool dryRun);

    // Stop the agent loop.
    void stop();

    // User approved a pending approval action.
    void approveAction(int approvalId);

    // User denied a pending approval action.
    void denyAction(int approvalId);

    bool isRunning() const
    {
        return m_running;
    }
    int iteration() const
    {
        return m_iteration;
    }
    int toolCallCount() const
    {
        return m_toolCallCount;
    }
    EditorContext *editorContext() const
    {
        return m_editorContext;
    }

    // Access the accumulated diff for the UI
    QString accumulatedDiff() const
    {
        return m_accumulatedDiff;
    }
    QList<PlanStep> currentPlan() const
    {
        return m_plan;
    }

signals:
    void logMessage(const QString &msg);
    void streamingToken(const QString &token);
    void planUpdated(const QList<PlanStep> &steps);
    void diffAvailable(const QString &diff);
    void approvalRequested(int id, const QString &action, const QString &reason,
                           const QString &preview);
    void iterationChanged(int iteration);
    void stopped(const QString &summary);
    void errorOccurred(const QString &error);

private:
    void runNextIteration();
    void handleResponse(const QString &response, const QString &error);
    void executeTool(const QString &name, const QJsonObject &args);
    QString buildSystemPrompt() const;

    IAIProvider *m_provider = nullptr;
    QList<IAIProvider *> m_allProviders;
    ToolRegistry *m_toolRegistry = nullptr;
    EditorContext *m_editorContext = nullptr;
    SafetyPolicy *m_safetyPolicy = nullptr;

    bool m_running = false;
    bool m_dryRun = true;
    int m_iteration = 0;
    int m_toolCallCount = 0;
    int m_textRetries = 0;  // consecutive text/error responses without valid JSON

    QString m_goal;
    QList<ChatMessage> m_messages;
    QList<PlanStep> m_plan;
    QString m_accumulatedDiff;

    // Pending approval
    struct PendingApproval
    {
        int id;
        QString toolName;
        QJsonObject toolArgs;
    };
    QList<PendingApproval> m_pendingApprovals;
    int m_nextApprovalId = 1;
};

}  // namespace qcai2
