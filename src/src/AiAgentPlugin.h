/*! Declares the Qt Creator plugin entry point that wires the AI agent feature set. */
#pragma once

#include <extensionsystem/iplugin.h>

namespace qcai2
{

class AgentController;
class AgentDockWidget;
class EditorContext;
class ToolRegistry;
class SafetyPolicy;
class IAIProvider;
class AiCompletionProvider;
class GhostTextManager;
class CopilotProvider;
class IdeOutputCapture;

namespace Internal
{

/** Qt Creator plugin that assembles providers, tools, controller state, and UI. */
class AiAgentPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "qcai2.json")

public:
    /**
     * Creates the plugin instance.
     */
    AiAgentPlugin();

    /**
     * Releases provider objects owned through QObject parentage.
     */
    ~AiAgentPlugin() final;

    /**
     * Initializes settings, tools, providers, and editor integrations.
     */
    void initialize() final;

    /**
     * Creates the dock widget after dependent plugins finish initializing.
     */
    void extensionsInitialized() final;

    /**
     * Stops active work and persists settings before Qt Creator shuts down.
     */
    ShutdownFlag aboutToShutdown() final;

private:
    /**
     * Creates the dock widget once the main window is available.
     */
    void createDockWidget();

    /**
     * Instantiates all supported providers and selects the active one.
     */
    void setupProviders();

    /**
     * Refreshes the cached GitHub Copilot model list asynchronously.
     */
    void refreshCopilotModels();

    /**
     * Registers the tool set exposed to the agent controller.
     */
    void registerTools();

    /** Agent loop coordinator shared with the dock widget. */
    AgentController *m_controller = nullptr;

    /** Captures active editor and project context for prompts. */
    EditorContext *m_editorContext = nullptr;

    /** Registry that owns the tools available to the model. */
    ToolRegistry *m_toolRegistry = nullptr;

    /** Safety limits applied to iterations, tool calls, and patch size. */
    SafetyPolicy *m_safetyPolicy = nullptr;

    /** Provider currently selected in settings. */
    IAIProvider *m_currentProvider = nullptr;

    /** Provider instances available for chat and completion requests. */
    QList<IAIProvider *> m_providers;

    /** Dedicated handle used to refresh GitHub Copilot model metadata. */
    CopilotProvider *m_copilotProvider = nullptr;

    /** Completion assist provider attached to text documents. */
    AiCompletionProvider *m_completionProvider = nullptr;

    /** Ghost-text overlay manager attached to editor widgets. */
    GhostTextManager *m_ghostTextManager = nullptr;

    /** Captures Qt Creator Compile Output and Application Output for agent tools. */
    IdeOutputCapture *m_outputCapture = nullptr;

};

}  // namespace Internal
}  // namespace qcai2
