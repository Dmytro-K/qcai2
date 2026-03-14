/*! @file
    @brief Implements the plan-act-observe loop that drives one agent session.
*/

#include "AgentController.h"
#include "mcp/McpToolManager.h"
#include "settings/Settings.h"
#include "util/Diff.h"
#include "util/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <utility>

namespace qcai2
{

namespace
{

constexpr int kDefaultPromptResultLimit = 15000;
constexpr int kVerboseToolPromptResultLimit = 4000;
constexpr int kProviderInactivityTimeoutMs = 75000;

QString asIndentedCodeBlock(const QString &text)
{
    const QString normalized = text.isEmpty() ? QStringLiteral("(empty)") : text;
    QStringList lines = normalized.split(QLatin1Char('\n'));
    for (QString &line : lines)
        line.prepend(QStringLiteral("    "));
    return lines.join(QLatin1Char('\n'));
}

QString formatToolArgs(const QJsonObject &args)
{
    return QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Indented)).trimmed();
}

QString formatToolExecutionLog(const QString &name, const QJsonObject &args)
{
    return QStringLiteral("🔧 **Tool:** `%1`\n\n**Arguments**\n\n%2")
        .arg(name, asIndentedCodeBlock(formatToolArgs(args)));
}

QString formatToolResultLog(const QString &result)
{
    static constexpr int kMaxLoggedResultLength = 500;

    QString preview = result;
    if (preview.size() > kMaxLoggedResultLength)
    {
        preview = preview.left(kMaxLoggedResultLength).trimmed();
        preview += QStringLiteral("\n... [truncated, %1 chars total]").arg(result.size());
    }

    return QStringLiteral("✅ **Result**\n\n%1").arg(asIndentedCodeBlock(preview.trimmed()));
}

int promptResultLimitForTool(const QString &name)
{
    if (name == QStringLiteral("show_compile_output") ||
        name == QStringLiteral("show_application_output") ||
        name == QStringLiteral("show_diagnostics"))
    {
        return kVerboseToolPromptResultLimit;
    }

    return kDefaultPromptResultLimit;
}

bool isEnabledEffort(const QString &level)
{
    return level == QStringLiteral("low") || level == QStringLiteral("medium") ||
           level == QStringLiteral("high");
}

QString runModeLabel(AgentController::RunMode runMode)
{
    return runMode == AgentController::RunMode::Ask ? QStringLiteral("ask")
                                                    : QStringLiteral("agent");
}

}  // namespace

AgentController::AgentController(QObject *parent) : QObject(parent)
{
    m_providerWatchdog.setSingleShot(true);
    m_providerWatchdog.setInterval(kProviderInactivityTimeoutMs);
    connect(&m_providerWatchdog, &QTimer::timeout, this,
            &AgentController::handleProviderInactivityTimeout);
}

void AgentController::setProvider(IAIProvider *provider)
{
    m_provider = provider;
}
void AgentController::setProviders(const QList<IAIProvider *> &providers)
{
    m_allProviders = providers;
}
void AgentController::setToolRegistry(ToolRegistry *registry)
{
    m_toolRegistry = registry;
}
void AgentController::setEditorContext(EditorContext *ctx)
{
    m_editorContext = ctx;
}
void AgentController::setMcpToolManager(McpToolManager *manager)
{
    m_mcpToolManager = manager;
}
void AgentController::setSafetyPolicy(SafetyPolicy *policy)
{
    m_safetyPolicy = policy;
}

void AgentController::setRequestContext(const QString &context, const QStringList &linkedFiles)
{
    m_requestContext = context;
    m_linkedFiles = linkedFiles;
}

QString AgentController::buildSystemPrompt() const
{
    QString sys;
    if (m_runMode == RunMode::Ask)
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
            "\"preview\":\"details\"}\n\n");
    }

    // Available tools
    if (m_runMode == RunMode::Agent && (m_toolRegistry != nullptr))
    {
        QJsonDocument toolsDoc(m_toolRegistry->toolDescriptionsJson());
        sys += QStringLiteral("Available tools:\n%1\n\n")
                   .arg(QString::fromUtf8(toolsDoc.toJson(QJsonDocument::Indented)));
    }

    // Editor context
    if (m_editorContext != nullptr)
    {
        sys += QStringLiteral("Current editor context:\n%1\n")
                   .arg(m_editorContext->toPromptFragment());

        // Include file contents from open editors
        const QString files = m_editorContext->fileContentsFragment(30000);
        if (!files.isEmpty())
            sys += QStringLiteral("File contents (from open tabs):\n%1\n").arg(files);
    }

    // Safety info
    if (m_dryRun)
    {
        sys += QStringLiteral(
            "MODE: DRY-RUN. Do NOT apply patches; only produce diffs for preview.\n");
    }

    if (m_runMode == RunMode::Agent)
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

void AgentController::start(const QString &goal, bool dryRun, RunMode runMode,
                            const QString &modelName, const QString &reasoningEffort,
                            const QString &thinkingLevel)
{
    if (m_running)
        return;

    // Re-select and reconfigure provider from current settings
    auto &s = settings();
    s.load();
    Logger::instance().setEnabled(s.debugLogging);
    if (!m_allProviders.isEmpty())
    {
        m_provider = nullptr;
        QCAI_DEBUG("Agent", QStringLiteral("Selecting provider '%1' from %2 available")
                                .arg(s.provider)
                                .arg(m_allProviders.size()));
        for (auto *p : m_allProviders)
        {
            if (p->id() == s.provider)
            {
                m_provider = p;
                break;
            }
        }
        if (m_provider == nullptr)
            m_provider = m_allProviders.first();

        // Reconfigure the selected provider
        if (s.provider == QStringLiteral("openai"))
        {
            m_provider->setBaseUrl(s.baseUrl);
            m_provider->setApiKey(s.apiKey);
        }
        else if (s.provider == QStringLiteral("local"))
        {
            m_provider->setBaseUrl(s.localBaseUrl);
            m_provider->setApiKey(s.apiKey);
        }
        else if (s.provider == QStringLiteral("ollama"))
        {
            m_provider->setBaseUrl(s.ollamaBaseUrl);
        }
    }

    if (m_provider == nullptr)
    {
        emit errorOccurred(QStringLiteral("No AI provider configured."));
        return;
    }

    QStringList mcpRefreshMessages;
    if (runMode == RunMode::Agent && m_mcpToolManager != nullptr)
    {
        QString projectDir;
        if (m_editorContext != nullptr)
            projectDir = m_editorContext->capture().projectDir;
        mcpRefreshMessages = m_mcpToolManager->refreshForProject(projectDir);
    }

    m_running = true;
    m_dryRun = dryRun;
    m_iteration = 0;
    m_toolCallCount = 0;
    m_textRetries = 0;
    m_modeRetries = 0;
    m_goal = goal;
    m_runMode = runMode;
    m_modelName = modelName.trimmed().isEmpty() ? s.modelName : modelName.trimmed();
    m_reasoningEffort = reasoningEffort.trimmed().isEmpty() ? s.reasoningEffort
                                                            : reasoningEffort.trimmed();
    m_thinkingLevel = thinkingLevel.trimmed().isEmpty() ? s.thinkingLevel
                                                        : thinkingLevel.trimmed();
    m_plan.clear();
    m_accumulatedDiff.clear();
    m_pendingApprovals.clear();
    m_messages.clear();

    QCAI_INFO("Agent",
              QStringLiteral(
                  "Starting run — mode: %1, provider: %2, model: %3, reasoning: %4, "
                  "thinking: %5, dryRun: %6, goal: %7")
                  .arg(runModeLabel(m_runMode), m_provider->id(), m_modelName, m_reasoningEffort,
                       m_thinkingLevel, dryRun ? QStringLiteral("yes") : QStringLiteral("no"),
                       goal.left(100)));

    // System prompt
    m_messages.append({QStringLiteral("system"), buildSystemPrompt()});

    if (!m_requestContext.trimmed().isEmpty())
        m_messages.append({QStringLiteral("system"), m_requestContext.trimmed()});

    if (isEnabledEffort(m_thinkingLevel))
        m_messages.append({QStringLiteral("system"),
                           QStringLiteral("Use %1 thinking depth for this task.")
                               .arg(m_thinkingLevel)});

    // User goal
    m_messages.append({QStringLiteral("user"), goal});

    emit logMessage(QStringLiteral("%1 %2 started. Goal: %3")
                        .arg(m_runMode == RunMode::Ask ? QStringLiteral("💬")
                                                       : QStringLiteral("▶"))
                        .arg(m_runMode == RunMode::Ask ? QStringLiteral("Ask mode")
                                                       : QStringLiteral("Agent"))
                        .arg(goal));
    if (!m_linkedFiles.isEmpty())
        emit logMessage(QStringLiteral("📎 Linked files: %1").arg(m_linkedFiles.join(QStringLiteral(", "))));
    for (const QString &message : std::as_const(mcpRefreshMessages))
        emit logMessage(message);
    runNextIteration();
}

void AgentController::stop()
{
    if (!m_running)
        return;
    m_running = false;
    QCAI_INFO("Agent", QStringLiteral("Agent stopped by user at iteration %1, tool calls: %2")
                           .arg(m_iteration)
                           .arg(m_toolCallCount));
    if (m_provider != nullptr)
        m_provider->cancel();
    disarmProviderWatchdog();
    emit logMessage(QStringLiteral("⏹ Agent stopped by user."));
    emit stopped(QStringLiteral("Stopped by user at iteration %1.").arg(m_iteration));
}

void AgentController::runNextIteration()
{
    if (!m_running)
        return;

    // Check limits
    const int maxIter =
        (m_safetyPolicy != nullptr) ? m_safetyPolicy->maxIterations() : settings().maxIterations;
    if (m_iteration >= maxIter)
    {
        m_running = false;
        emit logMessage(QStringLiteral("⚠ Max iterations reached (%1).").arg(maxIter));
        emit stopped(QStringLiteral("Max iterations reached."));
        return;
    }

    ++m_iteration;
    emit iterationChanged(m_iteration);
    emit logMessage(QStringLiteral("── Iteration %1 ──").arg(m_iteration));

    const auto &s = settings();
    QCAI_DEBUG("Agent",
               QStringLiteral(
                    "Iteration %1: sending %2 messages to %3 (model: %4, reasoning: %5, "
                    "thinking: %6, temp: %7)")
                    .arg(m_iteration)
                    .arg(m_messages.size())
                    .arg(m_provider->id(), m_modelName)
                    .arg(m_reasoningEffort)
                     .arg(m_thinkingLevel)
                     .arg(s.temperature));

    m_waitingForProvider = true;
    m_providerActivitySeen = false;
    emit logMessage(QStringLiteral("⏳ Waiting for model response..."));
    armProviderWatchdog();

    m_provider->complete(
        m_messages, m_modelName, s.temperature, s.maxTokens, m_reasoningEffort,
        [this](const QString &response, const QString &error) {
            QTimer::singleShot(0, this,
                               [this, response, error]() { handleResponse(response, error); });
        },
        [this](const QString &delta) {
            if (!delta.isEmpty())
            {
                if (!m_providerActivitySeen)
                {
                    m_providerActivitySeen = true;
                    emit logMessage(QStringLiteral("✍ Model response in progress..."));
                }
                armProviderWatchdog();
                emit streamingToken(delta);
            }
        });
}

void AgentController::handleResponse(const QString &response, const QString &error)
{
    if (!m_running)
        return;

    disarmProviderWatchdog();

    if (!error.isEmpty())
    {
        QCAI_ERROR("Agent", QStringLiteral("Provider error: %1").arg(error));
        emit logMessage(QStringLiteral("❌ Provider error: %1").arg(error));
        emit errorOccurred(error);
        m_running = false;
        emit stopped(QStringLiteral("Provider error."));
        return;
    }

    // Add assistant message to history
    m_messages.append({QStringLiteral("assistant"), response});

    // Parse response
    AgentResponse parsed = AgentResponse::parse(response);
    QCAI_DEBUG("Agent", QStringLiteral("Parsed response type: %1, length: %2")
                            .arg(static_cast<int>(parsed.type))
                            .arg(response.length()));

    // Reset text retry counter on valid JSON
    if (parsed.type != ResponseType::Text && parsed.type != ResponseType::Error)
        m_textRetries = 0;

    if (m_runMode == RunMode::Ask && parsed.type != ResponseType::Final &&
        parsed.type != ResponseType::Text && parsed.type != ResponseType::Error)
    {
        if (m_modeRetries >= 1)
        {
            emit logMessage(QStringLiteral("⚠ Ask mode received a non-final response twice."));
            m_running = false;
            emit stopped(QStringLiteral("Ask mode received an unsupported response."));
            return;
        }

        ++m_modeRetries;
        emit logMessage(QStringLiteral("ℹ Ask mode requested a direct final response."));
        m_messages.append({QStringLiteral("user"),
                           QStringLiteral("Ask mode is enabled. Reply with one JSON object of "
                                          "type \"final\" only. Do not plan, call tools, or ask "
                                          "for approval.")});
        runNextIteration();
        return;
    }

    switch (parsed.type)
    {
        case ResponseType::Plan:
            m_plan = parsed.steps;
            emit planUpdated(m_plan);
            emit logMessage(
                QStringLiteral("📋 Plan with %1 step(s) received.").arg(m_plan.size()));
            // Ask model to start executing the plan
            m_messages.append(
                {QStringLiteral("user"),
                 QStringLiteral("Good plan. Now execute step 1 using the available tools.")});
            runNextIteration();
            break;

        case ResponseType::ToolCall:
            executeTool(parsed.toolName, parsed.toolArgs);
            break;

        case ResponseType::Final:
            emit logMessage(QStringLiteral("✅ Agent finished: %1").arg(parsed.summary));
            if (!parsed.diff.isEmpty())
            {
                // Strip "=== NEW FILE:" blocks — only show unified diff of modified files
                auto nf = Diff::extractAndCreateNewFiles(parsed.diff, QString(), /*dryRun=*/true);
                const QString cleanDiff = nf.remainingDiff.trimmed();
                m_accumulatedDiff = Diff::normalize(cleanDiff);
                if (!m_accumulatedDiff.isEmpty())
                    emit diffAvailable(m_accumulatedDiff);
            }
            m_running = false;
            emit stopped(parsed.summary);
            break;

        case ResponseType::NeedApproval: {
            PendingApproval pa;
            pa.id = m_nextApprovalId++;
            pa.toolName = parsed.approvalAction;
            m_pendingApprovals.append(pa);
            emit approvalRequested(pa.id, parsed.approvalAction, parsed.approvalReason,
                                   parsed.approvalPreview);
            emit logMessage(
                QStringLiteral("⏸ Waiting for approval: %1").arg(parsed.approvalAction));
            break;
        }

        case ResponseType::Text:
        case ResponseType::Error:
            // If model keeps responding with text, treat it as final answer
            if (m_textRetries >= 1)
            {
                if (settings().agentDebug)
                    emit logMessage(QStringLiteral("💬 %1").arg(response.left(1000)));
                else
                    emit logMessage(
                        QStringLiteral("💬 Agent finished with unstructured response."));
                m_running = false;
                emit stopped(QStringLiteral("Agent finished (text response)."));
            }
            else
            {
                ++m_textRetries;
                emit logMessage(QStringLiteral("ℹ Raw text response, asking for JSON format."));
                m_messages.append({QStringLiteral("user"),
                                   QStringLiteral("Please respond in the required JSON format "
                                                  "(plan, tool_call, final, or need_approval).")});
                runNextIteration();
            }
            break;
    }
}

void AgentController::executeTool(const QString &name, const QJsonObject &args)
{
    if (!m_running)
        return;

    const int maxCalls = (m_safetyPolicy != nullptr) ? m_safetyPolicy->maxToolCalls() : settings().maxToolCalls;
    if (m_toolCallCount >= maxCalls)
    {
        m_running = false;
        emit logMessage(QStringLiteral("⚠ Max tool calls reached (%1).").arg(maxCalls));
        emit stopped(QStringLiteral("Max tool calls reached."));
        return;
    }

    ITool *tool = (m_toolRegistry != nullptr) ? m_toolRegistry->tool(name) : nullptr;
    if (tool == nullptr)
    {
        m_messages.append(
            {QStringLiteral("user"),
             QStringLiteral("Error: unknown tool '%1'. Use one of the available tools.")
                 .arg(name)});
        runNextIteration();
        return;
    }

    // Check if approval is required
    if (tool->requiresApproval() && !m_dryRun)
    {
        QString reason = (m_safetyPolicy != nullptr) ? m_safetyPolicy->requiresApproval(name)
                                        : QStringLiteral("Tool requires approval.");

        if (!reason.isEmpty())
        {
            PendingApproval pa;
            pa.id = m_nextApprovalId++;
            pa.toolName = name;
            pa.toolArgs = args;
            m_pendingApprovals.append(pa);
            emit approvalRequested(
                pa.id, name, reason,
                QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Indented)));
            emit logMessage(QStringLiteral("⏸ Approval required for '%1': %2").arg(name, reason));
            return;
        }
    }

    // Execute
    ++m_toolCallCount;
    const bool suppressReadFileLog = (name == QStringLiteral("read_file"));
    if (!suppressReadFileLog)
        emit logMessage(formatToolExecutionLog(name, args));
    QCAI_DEBUG(
        "Agent",
        QStringLiteral("Tool call #%1: %2, args: %3")
            .arg(m_toolCallCount)
            .arg(name,
                 QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).left(200)));

    // Get workDir from editor context
    QString workDir;
    if (m_editorContext != nullptr)
    {
        auto snap = m_editorContext->capture();
        workDir = snap.projectDir;
    }

    QString result = tool->execute(args, workDir);
    if (!suppressReadFileLog)
        emit logMessage(formatToolResultLog(result));
    QCAI_DEBUG("Agent",
               QStringLiteral("Tool '%1' result length: %2").arg(name).arg(result.length()));

    // Feed result back to LLM
    const int maxResultLen = promptResultLimitForTool(name);
    const QString truncatedResult =
        result.length() > maxResultLen
            ? result.left(maxResultLen) +
                  QStringLiteral("\n... [truncated, %1 chars total]").arg(result.length())
            : result;
    m_messages.append({QStringLiteral("user"),
                       QStringLiteral("Tool '%1' returned:\n%2\n\nContinue with the next step.")
                           .arg(name, truncatedResult)});
    runNextIteration();
}

void AgentController::armProviderWatchdog()
{
    if (m_running && m_waitingForProvider)
        m_providerWatchdog.start();
}

void AgentController::disarmProviderWatchdog()
{
    m_providerWatchdog.stop();
    m_waitingForProvider = false;
    m_providerActivitySeen = false;
}

void AgentController::handleProviderInactivityTimeout()
{
    if (!m_running || !m_waitingForProvider)
        return;

    QCAI_ERROR("Agent", QStringLiteral("Provider response timed out after %1 ms of inactivity")
                            .arg(kProviderInactivityTimeoutMs));
    if (m_provider != nullptr)
        m_provider->cancel();

    emit logMessage(QStringLiteral("⌛ Provider response timed out after %1 seconds of inactivity.")
                        .arg(kProviderInactivityTimeoutMs / 1000));
    emit errorOccurred(QStringLiteral("Provider response timed out."));
    m_running = false;
    disarmProviderWatchdog();
    emit stopped(QStringLiteral("Provider response timed out."));
}

void AgentController::approveAction(int approvalId)
{
    for (int i = 0; i < m_pendingApprovals.size(); ++i)
    {
        if (m_pendingApprovals[i].id == approvalId)
        {
            auto pa = m_pendingApprovals.takeAt(i);
            emit logMessage(QStringLiteral("✅ Approved: %1").arg(pa.toolName));

            if (!pa.toolName.isEmpty() && (m_toolRegistry != nullptr))
            {
                ITool *tool = m_toolRegistry->tool(pa.toolName);
                if (tool != nullptr)
                {
                    ++m_toolCallCount;
                    QString workDir;
                    if (m_editorContext != nullptr)
                        workDir = m_editorContext->capture().projectDir;
                    QString result = tool->execute(pa.toolArgs, workDir);
                    m_messages.append(
                        {QStringLiteral("user"),
                         QStringLiteral("Approved and executed '%1'. Result:\n%2\n\nContinue.")
                             .arg(pa.toolName, result)});
                }
            }
            runNextIteration();
            return;
        }
    }
}

void AgentController::denyAction(int approvalId)
{
    for (int i = 0; i < m_pendingApprovals.size(); ++i)
    {
        if (m_pendingApprovals[i].id == approvalId)
        {
            auto pa = m_pendingApprovals.takeAt(i);
            emit logMessage(QStringLiteral("❌ Denied: %1").arg(pa.toolName));
            m_messages.append(
                {QStringLiteral("user"),
                 QStringLiteral(
                     "The user denied the action '%1'. Find an alternative approach or finish.")
                     .arg(pa.toolName)});
            runNextIteration();
            return;
        }
    }
}

}  // namespace qcai2
