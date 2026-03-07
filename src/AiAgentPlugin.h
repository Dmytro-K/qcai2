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

namespace Internal
{

class AiAgentPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "qcai2.json")

public:
    AiAgentPlugin();
    ~AiAgentPlugin() final;

    void initialize() final;
    void extensionsInitialized() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void createDockWidget();
    void setupProviders();
    void refreshCopilotModels();
    void registerTools();

    AgentController *m_controller = nullptr;
    EditorContext *m_editorContext = nullptr;
    ToolRegistry *m_toolRegistry = nullptr;
    SafetyPolicy *m_safetyPolicy = nullptr;
    IAIProvider *m_currentProvider = nullptr;

    // Provider instances
    QList<IAIProvider *> m_providers;
    CopilotProvider *m_copilotProvider = nullptr;
    AiCompletionProvider *m_completionProvider = nullptr;
    GhostTextManager *m_ghostTextManager = nullptr;
};

}  // namespace Internal
}  // namespace qcai2
