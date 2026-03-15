/*! @file
    @brief Implements the plan-act-observe loop that drives one agent session.
*/

#include "AgentController.h"
#include "context/ChatContextManager.h"
#include "mcp/McpToolManager.h"
#include "settings/Settings.h"
#include "util/Diff.h"
#include "util/Logger.h"

#include <QFileInfo>
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
constexpr int kProviderThinkingInactivityTimeoutMs = 180000;

QString asIndentedCodeBlock(const QString &text)
{
    const QString normalized = text.isEmpty() ? QStringLiteral("(empty)") : text;
    QStringList lines = normalized.split(QLatin1Char('\n'));
    for (QString &line : lines)
    {
        line.prepend(QStringLiteral("    "));
    }
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

ContextRequestKind contextRequestKind(AgentController::RunMode runMode)
{
    return runMode == AgentController::RunMode::Ask ? ContextRequestKind::Ask
                                                    : ContextRequestKind::AgentChat;
}

ProviderUsage accumulateUsage(const ProviderUsage &lhs, const ProviderUsage &rhs)
{
    ProviderUsage usage;
    usage.inputTokens = (lhs.inputTokens >= 0 || rhs.inputTokens >= 0)
                            ? qMax(0, qMax(lhs.inputTokens, 0) + qMax(rhs.inputTokens, 0))
                            : -1;
    usage.outputTokens = (lhs.outputTokens >= 0 || rhs.outputTokens >= 0)
                             ? qMax(0, qMax(lhs.outputTokens, 0) + qMax(rhs.outputTokens, 0))
                             : -1;
    usage.reasoningTokens =
        (lhs.reasoningTokens >= 0 || rhs.reasoningTokens >= 0)
            ? qMax(0, qMax(lhs.reasoningTokens, 0) + qMax(rhs.reasoningTokens, 0))
            : -1;
    usage.cachedInputTokens =
        (lhs.cachedInputTokens >= 0 || rhs.cachedInputTokens >= 0)
            ? qMax(0, qMax(lhs.cachedInputTokens, 0) + qMax(rhs.cachedInputTokens, 0))
            : -1;
    usage.totalTokens =
        (lhs.resolvedTotalTokens() >= 0 || rhs.resolvedTotalTokens() >= 0)
            ? qMax(0, qMax(lhs.resolvedTotalTokens(), 0) + qMax(rhs.resolvedTotalTokens(), 0))
            : -1;
    return usage;
}

QString artifactKindForTool(const QString &toolName)
{
    if (toolName == QStringLiteral("run_build"))
    {
        return QStringLiteral("build_log");
    }
    if (toolName == QStringLiteral("run_tests"))
    {
        return QStringLiteral("test_result");
    }
    if (toolName == QStringLiteral("git_diff") || toolName == QStringLiteral("apply_patch"))
    {
        return QStringLiteral("diff");
    }
    if (toolName == QStringLiteral("read_file"))
    {
        return QStringLiteral("file_snippet");
    }
    return QStringLiteral("tool_output");
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
void AgentController::setChatContextManager(ChatContextManager *manager)
{
    m_chatContextManager = manager;
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
        {
            sys += QStringLiteral("File contents (from open tabs):\n%1\n").arg(files);
        }
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
    {
        return;
    }

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
        {
            m_provider = m_allProviders.first();
        }

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
        {
            projectDir = m_editorContext->capture().projectDir;
        }
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
    m_reasoningEffort =
        reasoningEffort.trimmed().isEmpty() ? s.reasoningEffort : reasoningEffort.trimmed();
    m_thinkingLevel =
        thinkingLevel.trimmed().isEmpty() ? s.thinkingLevel : thinkingLevel.trimmed();
    m_plan.clear();
    m_accumulatedDiff.clear();
    m_pendingApprovals.clear();
    m_messages.clear();
    m_runId.clear();
    m_accumulatedUsage = {};

    QCAI_INFO("Agent",
              QStringLiteral("Starting run — mode: %1, provider: %2, model: %3, reasoning: %4, "
                             "thinking: %5, dryRun: %6, goal: %7")
                  .arg(runModeLabel(m_runMode), m_provider->id(), m_modelName, m_reasoningEffort,
                       m_thinkingLevel, dryRun ? QStringLiteral("yes") : QStringLiteral("no"),
                       goal.left(100)));

    QStringList dynamicSystemMessages;
    if (m_requestContext.trimmed().isEmpty() == false)
    {
        dynamicSystemMessages.append(m_requestContext.trimmed());
    }
    if (isEnabledEffort(m_thinkingLevel) == true)
    {
        dynamicSystemMessages.append(
            QStringLiteral("Use %1 thinking depth for this task.").arg(m_thinkingLevel));
    }

    bool restoredPersistentContext = false;
    if ((m_chatContextManager != nullptr) && (m_editorContext != nullptr))
    {
        const EditorContext::Snapshot snapshot = m_editorContext->capture();
        QString workspaceRoot = snapshot.projectDir;
        if (workspaceRoot.isEmpty() == true && snapshot.filePath.isEmpty() == false)
        {
            workspaceRoot = QFileInfo(snapshot.filePath).absolutePath();
        }
        QString workspaceId = snapshot.projectFilePath;
        if (workspaceId.isEmpty() == true)
        {
            workspaceId = workspaceRoot;
        }

        if (workspaceRoot.isEmpty() == false && workspaceId.isEmpty() == false)
        {
            QString contextError;
            const QString existingConversationId =
                m_chatContextManager->activeWorkspaceId() == workspaceId
                    ? m_chatContextManager->activeConversationId()
                    : QString();
            if (m_chatContextManager->setActiveWorkspace(
                    workspaceId, workspaceRoot, existingConversationId, &contextError) == true)
            {
                m_chatContextManager->syncWorkspaceState(snapshot, s, &contextError);
                if (contextError.isEmpty() == true)
                {
                    const QJsonObject runMetadata{
                        {QStringLiteral("goalPreview"), goal.left(200)},
                        {QStringLiteral("projectFilePath"), snapshot.projectFilePath},
                        {QStringLiteral("projectDir"), snapshot.projectDir},
                        {QStringLiteral("linkedFileCount"), m_linkedFiles.size()},
                        {QStringLiteral("mode"), runModeLabel(m_runMode)},
                    };
                    m_runId = m_chatContextManager->beginRun(
                        contextRequestKind(m_runMode), m_provider->id(), m_modelName,
                        m_reasoningEffort, m_thinkingLevel, m_dryRun, runMetadata, &contextError);
                }

                if (contextError.isEmpty() == true &&
                    m_requestContext.trimmed().isEmpty() == false)
                {
                    const QJsonObject requestContextMetadata{
                        {QStringLiteral("linkedFiles"), QJsonArray::fromStringList(m_linkedFiles)},
                    };
                    m_chatContextManager->appendArtifact(
                        m_runId, QStringLiteral("request_context"),
                        QStringLiteral("request_context"), m_requestContext.trimmed(),
                        requestContextMetadata, &contextError);
                }

                if (contextError.isEmpty() == true)
                {
                    const QJsonObject goalMetadata{
                        {QStringLiteral("mode"), runModeLabel(m_runMode)},
                        {QStringLiteral("linkedFileCount"), m_linkedFiles.size()},
                    };
                    m_chatContextManager->appendUserMessage(m_runId, goal, QStringLiteral("goal"),
                                                            goalMetadata, &contextError);
                }

                if (contextError.isEmpty() == true)
                {
                    const ContextEnvelope envelope = m_chatContextManager->buildContextEnvelope(
                        contextRequestKind(m_runMode), buildSystemPrompt(), dynamicSystemMessages,
                        s.maxTokens, &contextError);
                    if (contextError.isEmpty() == true &&
                        envelope.providerMessages.isEmpty() == false)
                    {
                        m_messages = envelope.providerMessages;
                        restoredPersistentContext = true;
                    }
                }
            }

            if (contextError.isEmpty() == false)
            {
                if ((m_chatContextManager != nullptr) && (m_runId.isEmpty() == false))
                {
                    QString finishError;
                    m_chatContextManager->finishRun(
                        m_runId, QStringLiteral("error"), m_accumulatedUsage,
                        QJsonObject{{QStringLiteral("error"), contextError},
                                    {QStringLiteral("reason"),
                                     QStringLiteral("context-bootstrap-failed")}},
                        &finishError);
                }
                emit logMessage(
                    QStringLiteral("⚠ Persistent chat context unavailable: %1").arg(contextError));
                m_runId.clear();
            }
        }
    }

    if (restoredPersistentContext == false)
    {
        m_messages.append({QStringLiteral("system"), buildSystemPrompt()});

        for (const QString &message : dynamicSystemMessages)
        {
            m_messages.append({QStringLiteral("system"), message});
        }

        m_messages.append({QStringLiteral("user"), goal});
    }

    emit logMessage(
        QStringLiteral("%1 %2 started. Goal: %3")
            .arg(m_runMode == RunMode::Ask ? QStringLiteral("💬") : QStringLiteral("▶"))
            .arg(m_runMode == RunMode::Ask ? QStringLiteral("Ask mode") : QStringLiteral("Agent"))
            .arg(goal));
    if (!m_linkedFiles.isEmpty())
    {
        emit logMessage(
            QStringLiteral("📎 Linked files: %1").arg(m_linkedFiles.join(QStringLiteral(", "))));
    }
    for (const QString &message : std::as_const(mcpRefreshMessages))
    {
        emit logMessage(message);
    }
    runNextIteration();
}

void AgentController::stop()
{
    if (!m_running)
    {
        return;
    }
    m_running = false;
    QCAI_INFO("Agent", QStringLiteral("Agent stopped by user at iteration %1, tool calls: %2")
                           .arg(m_iteration)
                           .arg(m_toolCallCount));
    if (m_provider != nullptr)
    {
        m_provider->cancel();
    }
    disarmProviderWatchdog();
    emit logMessage(QStringLiteral("⏹ Agent stopped by user."));
    finalizePersistentRun(QStringLiteral("stopped"),
                          QJsonObject{{QStringLiteral("reason"), QStringLiteral("user-stop")}});
    emit stopped(QStringLiteral("Stopped by user at iteration %1.").arg(m_iteration));
}

void AgentController::runNextIteration()
{
    if (!m_running)
    {
        return;
    }

    // Check limits
    const int maxIter =
        (m_safetyPolicy != nullptr) ? m_safetyPolicy->maxIterations() : settings().maxIterations;
    if (m_iteration >= maxIter)
    {
        m_running = false;
        emit logMessage(QStringLiteral("⚠ Max iterations reached (%1).").arg(maxIter));
        finalizePersistentRun(
            QStringLiteral("error"),
            QJsonObject{{QStringLiteral("reason"), QStringLiteral("max-iterations")}});
        emit stopped(QStringLiteral("Max iterations reached."));
        return;
    }

    ++m_iteration;
    emit iterationChanged(m_iteration);
    emit logMessage(QStringLiteral("── Iteration %1 ──").arg(m_iteration));

    const auto &s = settings();
    QCAI_DEBUG("Agent",
               QStringLiteral("Iteration %1: sending %2 messages to %3 (model: %4, reasoning: %5, "
                              "thinking: %6, temp: %7)")
                   .arg(m_iteration)
                   .arg(m_messages.size())
                   .arg(m_provider->id(), m_modelName)
                   .arg(m_reasoningEffort)
                   .arg(m_thinkingLevel)
                   .arg(s.temperature));

    m_waitingForProvider = true;
    m_providerActivitySeen = false;
    m_providerRequestTimer.start();
    emit logMessage(QStringLiteral("⏳ Waiting for model response..."));
    armProviderWatchdog();

    m_provider->complete(
        m_messages, m_modelName, s.temperature, s.maxTokens, m_reasoningEffort,
        [this](const QString &response, const QString &error, const ProviderUsage &usage) {
            QTimer::singleShot(0, this, [this, response, error, usage]() {
                handleResponse(response, error, usage);
            });
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

void AgentController::handleResponse(const QString &response, const QString &error,
                                     const ProviderUsage &usage)
{
    if (!m_running)
    {
        return;
    }

    disarmProviderWatchdog();
    const qint64 requestDurationMs =
        (m_providerRequestTimer.isValid() == true) ? m_providerRequestTimer.elapsed() : -1;

    if (!error.isEmpty())
    {
        QCAI_ERROR("Agent", QStringLiteral("Provider error: %1").arg(error));
        emit logMessage(QStringLiteral("❌ Provider error: %1").arg(error));
        emit errorOccurred(error);
        m_running = false;
        finalizePersistentRun(QStringLiteral("error"),
                              QJsonObject{{QStringLiteral("error"), error}});
        emit stopped(QStringLiteral("Provider error."));
        return;
    }

    if ((usage.hasAny() == true) || (requestDurationMs >= 0))
    {
        m_accumulatedUsage = accumulateUsage(m_accumulatedUsage, usage);
        emit providerUsageAvailable(usage, requestDurationMs);
    }

    // Add assistant message to history
    appendAssistantHistoryMessage(response, QStringLiteral("model_response"),
                                  QJsonObject{{QStringLiteral("iteration"), m_iteration}});

    // Parse response
    AgentResponse parsed = AgentResponse::parse(response);
    QCAI_DEBUG("Agent", QStringLiteral("Parsed response type: %1, length: %2")
                            .arg(static_cast<int>(parsed.type))
                            .arg(response.length()));

    // Reset text retry counter on valid JSON
    if (parsed.type != ResponseType::Text && parsed.type != ResponseType::Error)
    {
        m_textRetries = 0;
    }

    if (m_runMode == RunMode::Ask && parsed.type != ResponseType::Final &&
        parsed.type != ResponseType::Text && parsed.type != ResponseType::Error)
    {
        if (m_modeRetries >= 1)
        {
            emit logMessage(QStringLiteral("⚠ Ask mode received a non-final response twice."));
            m_running = false;
            finalizePersistentRun(QStringLiteral("error"),
                                  QJsonObject{{QStringLiteral("reason"),
                                               QStringLiteral("ask-mode-non-final-response")}});
            emit stopped(QStringLiteral("Ask mode received an unsupported response."));
            return;
        }

        ++m_modeRetries;
        emit logMessage(QStringLiteral("ℹ Ask mode requested a direct final response."));
        appendControllerUserMessage(
            QStringLiteral(
                "Ask mode is enabled. Reply with one JSON object of type \"final\" only. "
                "Do not plan, call tools, or ask for approval."),
            QStringLiteral("controller_mode_correction"));
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
            appendControllerUserMessage(
                QStringLiteral("Good plan. Now execute step 1 using the available tools."),
                QStringLiteral("controller_plan_ack"));
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
                {
                    emit diffAvailable(m_accumulatedDiff);
                }
            }
            m_running = false;
            finalizePersistentRun(
                QStringLiteral("completed"),
                QJsonObject{{QStringLiteral("summary"), parsed.summary},
                            {QStringLiteral("responseType"), QStringLiteral("final")}});
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
                if (((settings().agentDebug) == true))
                {
                    emit logMessage(QStringLiteral("💬 %1").arg(response.left(1000)));
                }
                else
                {
                    emit logMessage(
                        QStringLiteral("💬 Agent finished with unstructured response."));
                }
                m_running = false;
                finalizePersistentRun(
                    QStringLiteral("completed"),
                    QJsonObject{{QStringLiteral("summary"),
                                 QStringLiteral("Agent finished (text response).")},
                                {QStringLiteral("responseType"), QStringLiteral("text")}});
                emit stopped(QStringLiteral("Agent finished (text response)."));
            }
            else
            {
                ++m_textRetries;
                emit logMessage(QStringLiteral("ℹ Raw text response, asking for JSON format."));
                appendControllerUserMessage(
                    QStringLiteral("Please respond in the required JSON format (plan, tool_call, "
                                   "final, or need_approval)."),
                    QStringLiteral("controller_json_retry"));
                runNextIteration();
            }
            break;
    }
}

void AgentController::executeTool(const QString &name, const QJsonObject &args)
{
    if (!m_running)
    {
        return;
    }

    const int maxCalls =
        (m_safetyPolicy != nullptr) ? m_safetyPolicy->maxToolCalls() : settings().maxToolCalls;
    if (m_toolCallCount >= maxCalls)
    {
        m_running = false;
        emit logMessage(QStringLiteral("⚠ Max tool calls reached (%1).").arg(maxCalls));
        finalizePersistentRun(
            QStringLiteral("error"),
            QJsonObject{{QStringLiteral("reason"), QStringLiteral("max-tool-calls")}});
        emit stopped(QStringLiteral("Max tool calls reached."));
        return;
    }

    ITool *tool = (m_toolRegistry != nullptr) ? m_toolRegistry->tool(name) : nullptr;
    if (tool == nullptr)
    {
        appendControllerUserMessage(
            QStringLiteral("Error: unknown tool '%1'. Use one of the available tools.").arg(name),
            QStringLiteral("controller_unknown_tool"),
            QJsonObject{{QStringLiteral("tool"), name}});
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
    {
        emit logMessage(formatToolExecutionLog(name, args));
    }
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
    if (m_chatContextManager != nullptr)
    {
        QString contextError;
        const QJsonObject artifactMetadata{
            {QStringLiteral("tool"), name},
            {QStringLiteral("args"), args},
            {QStringLiteral("iteration"), m_iteration},
            {QStringLiteral("toolCallCount"), m_toolCallCount},
        };
        if (m_chatContextManager->appendArtifact(m_runId, artifactKindForTool(name), name, result,
                                                 artifactMetadata, &contextError) == false &&
            contextError.isEmpty() == false)
        {
            emit logMessage(
                QStringLiteral("⚠ Failed to persist tool output context: %1").arg(contextError));
        }
    }
    if (!suppressReadFileLog)
    {
        emit logMessage(formatToolResultLog(result));
    }
    QCAI_DEBUG("Agent",
               QStringLiteral("Tool '%1' result length: %2").arg(name).arg(result.length()));

    // Feed result back to LLM
    const int maxResultLen = promptResultLimitForTool(name);
    const QString truncatedResult =
        result.length() > maxResultLen
            ? result.left(maxResultLen) +
                  QStringLiteral("\n... [truncated, %1 chars total]").arg(result.length())
            : result;
    appendControllerUserMessage(
        QStringLiteral("Tool '%1' returned:\n%2\n\nContinue with the next step.")
            .arg(name, truncatedResult),
        QStringLiteral("tool_result"),
        QJsonObject{{QStringLiteral("tool"), name},
                    {QStringLiteral("truncated"), result.length() > maxResultLen}});
    runNextIteration();
}

void AgentController::armProviderWatchdog()
{
    if (m_running && m_waitingForProvider)
    {
        const bool thinkingEnabled = !m_thinkingLevel.isEmpty() &&
                                     m_thinkingLevel != QStringLiteral("off") &&
                                     m_thinkingLevel != QStringLiteral("none");
        const int timeoutMs =
            thinkingEnabled ? kProviderThinkingInactivityTimeoutMs : kProviderInactivityTimeoutMs;
        m_providerWatchdog.setInterval(timeoutMs);
        m_providerWatchdog.start();
    }
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
    {
        return;
    }

    const int actualTimeoutMs = m_providerWatchdog.interval();
    QCAI_ERROR("Agent", QStringLiteral("Provider response timed out after %1 ms of inactivity")
                            .arg(actualTimeoutMs));
    if (m_provider != nullptr)
    {
        m_provider->cancel();
    }

    emit logMessage(
        QStringLiteral("⌛ Provider response timed out after %1 seconds of inactivity.")
            .arg(actualTimeoutMs / 1000));
    emit errorOccurred(QStringLiteral("Provider response timed out."));
    m_running = false;
    disarmProviderWatchdog();
    finalizePersistentRun(
        QStringLiteral("error"),
        QJsonObject{{QStringLiteral("reason"), QStringLiteral("provider-timeout")}});
    emit stopped(QStringLiteral("Provider response timed out."));
}

void AgentController::appendControllerUserMessage(const QString &content, const QString &source,
                                                  const QJsonObject &metadata)
{
    m_messages.append({QStringLiteral("user"), content});
    if (m_chatContextManager != nullptr)
    {
        QString contextError;
        if (m_chatContextManager->appendUserMessage(m_runId, content, source, metadata,
                                                    &contextError) == false &&
            contextError.isEmpty() == false)
        {
            emit logMessage(
                QStringLiteral("⚠ Failed to persist chat history: %1").arg(contextError));
        }
    }
}

void AgentController::appendAssistantHistoryMessage(const QString &content, const QString &source,
                                                    const QJsonObject &metadata)
{
    m_messages.append({QStringLiteral("assistant"), content});
    if (m_chatContextManager != nullptr)
    {
        QString contextError;
        if (m_chatContextManager->appendAssistantMessage(m_runId, content, source, metadata,
                                                         &contextError) == false &&
            contextError.isEmpty() == false)
        {
            emit logMessage(
                QStringLiteral("⚠ Failed to persist assistant history: %1").arg(contextError));
        }
    }
}

void AgentController::finalizePersistentRun(const QString &status, const QJsonObject &metadata)
{
    if (m_chatContextManager == nullptr || m_runId.isEmpty() == true)
    {
        return;
    }

    QString artifactError;
    if (m_accumulatedDiff.isEmpty() == false)
    {
        m_chatContextManager->appendArtifact(
            m_runId, QStringLiteral("diff"), QStringLiteral("final_diff"), m_accumulatedDiff,
            QJsonObject{{QStringLiteral("status"), status}}, &artifactError);
    }
    QString finishError;
    m_chatContextManager->finishRun(m_runId, status, m_accumulatedUsage, metadata, &finishError);

    if (artifactError.isEmpty() == false)
    {
        emit logMessage(
            QStringLiteral("⚠ Failed to persist final diff artifact: %1").arg(artifactError));
    }
    if (finishError.isEmpty() == false)
    {
        emit logMessage(
            QStringLiteral("⚠ Failed to finalize persistent chat context: %1").arg(finishError));
    }

    m_runId.clear();
}

void AgentController::approveAction(int approvalId)
{
    for (int i = 0; i < m_pendingApprovals.size(); ++i)
    {
        if (((m_pendingApprovals[i].id == approvalId) == true))
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
                    {
                        workDir = m_editorContext->capture().projectDir;
                    }
                    QString result = tool->execute(pa.toolArgs, workDir);
                    if (m_chatContextManager != nullptr)
                    {
                        QString contextError;
                        const QJsonObject artifactMetadata{
                            {QStringLiteral("tool"), pa.toolName},
                            {QStringLiteral("args"), pa.toolArgs},
                            {QStringLiteral("approved"), true},
                        };
                        if (m_chatContextManager->appendArtifact(
                                m_runId, artifactKindForTool(pa.toolName), pa.toolName, result,
                                artifactMetadata, &contextError) == false &&
                            contextError.isEmpty() == false)
                        {
                            emit logMessage(
                                QStringLiteral("⚠ Failed to persist approved tool output: %1")
                                    .arg(contextError));
                        }
                    }
                    appendControllerUserMessage(
                        QStringLiteral("Approved and executed '%1'. Result:\n%2\n\nContinue.")
                            .arg(pa.toolName, result),
                        QStringLiteral("approved_tool_result"),
                        QJsonObject{{QStringLiteral("tool"), pa.toolName}});
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
        if (((m_pendingApprovals[i].id == approvalId) == true))
        {
            auto pa = m_pendingApprovals.takeAt(i);
            emit logMessage(QStringLiteral("❌ Denied: %1").arg(pa.toolName));
            appendControllerUserMessage(
                QStringLiteral("The user denied the action '%1'. Find an alternative approach or "
                               "finish.")
                    .arg(pa.toolName),
                QStringLiteral("approval_denied"),
                QJsonObject{{QStringLiteral("tool"), pa.toolName}});
            runNextIteration();
            return;
        }
    }
}

}  // namespace qcai2
