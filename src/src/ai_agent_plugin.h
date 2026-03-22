/*! Declares the Qt Creator plugin entry point that wires the AI agent feature set. */
#pragma once

#include <extensionsystem/iplugin.h>

#include <functional>

namespace qcai2
{

class agent_controller_t;
class agent_dock_widget_t;
class editor_context_t;
class chat_context_manager_t;
class mcp_tool_manager_t;
class tool_registry_t;
class safety_policy_t;
class iai_provider_t;
class ai_completion_provider_t;
class ghost_text_manager_t;
class copilot_provider_t;
class ide_output_capture_t;
class debugger_session_service_t;
class clangd_service_t;
class vector_search_service_t;

namespace Internal
{

class ai_agent_navigation_widget_factory_t;

/** Qt Creator plugin that assembles providers, tools, controller state, and UI. */
class ai_agent_plugin_t final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "qcai2.json")

public:
    /**
     * Creates the plugin instance.
     */
    ai_agent_plugin_t();

    /**
     * Releases provider objects owned through QObject parentage.
     */
    ~ai_agent_plugin_t() final;

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
     * Activates the AI Agent sidebar widget once the main window is available.
     */
    void show_navigation_widget();

    /**
     * Activates the AI Agent sidebar widget and focuses the goal editor.
     */
    void focus_goal_input();

    /**
     * Activates the AI Agent sidebar widget and queues the current draft request.
     */
    void queue_current_request();

    /**
     * Activates the AI Agent sidebar widget, then invokes a callback on it.
     */
    void with_agent_dock_widget(const std::function<void(agent_dock_widget_t *)> &callback);

    /**
     * Instantiates all supported providers and selects the active one.
     */
    void setup_providers();

    /**
     * Refreshes the cached GitHub Copilot model list asynchronously.
     */
    void refresh_copilot_models();

    /**
     * Registers the tool set exposed to the agent controller.
     */
    void register_tools();

    /** Agent loop coordinator shared with the dock widget. */
    agent_controller_t *controller = nullptr;

    /** Captures active editor and project context for prompts. */
    editor_context_t *editor_context = nullptr;

    /** Owns persistent SQLite-backed chat context for each workspace. */
    chat_context_manager_t *chat_context_manager = nullptr;

    /** Loads configured MCP servers and exposes their tools at runtime. */
    mcp_tool_manager_t *mcp_tool_manager = nullptr;

    /** Registry that owns the tools available to the model. */
    tool_registry_t *tool_registry = nullptr;

    /** Safety limits applied to iterations, tool calls, and patch size. */
    safety_policy_t *safety_policy = nullptr;

    /** Provider currently selected in settings. */
    iai_provider_t *current_provider = nullptr;

    /** Provider instances available for chat and completion requests. */
    QList<iai_provider_t *> providers;

    /** Dedicated handle used to refresh GitHub Copilot model metadata. */
    copilot_provider_t *copilot_provider = nullptr;

    /** Completion assist provider attached to text documents. */
    ai_completion_provider_t *completion_provider = nullptr;

    /** Ghost-text overlay manager attached to editor widgets. */
    ghost_text_manager_t *ghost_text_manager = nullptr;

    /** Captures Qt Creator Compile Output and Application Output for agent tools. */
    ide_output_capture_t *output_capture = nullptr;

    /** Shared debugger-session service used by tools, output capture, and UI. */
    debugger_session_service_t *debugger_service = nullptr;

    /** Reusable ClangCodeModel/clangd bridge shared by tools and future features. */
    clangd_service_t *clangd_service = nullptr;

    /** Provider-agnostic vector search service shared by future retrieval features. */
    vector_search_service_t *vector_search_service = nullptr;

    /** Sidebar factory that hosts the AI Agent inside Qt Creator navigation panels. */
    ai_agent_navigation_widget_factory_t *navigation_widget_factory = nullptr;
};

}  // namespace Internal
}  // namespace qcai2
