#pragma once

#include <extensionsystem/iplugin.h>

namespace Qcai2 {

class AgentController;
class AgentDockWidget;
class EditorContext;
class ToolRegistry;
class SafetyPolicy;
class IAIProvider;
class AiCompletionProvider;

namespace Internal {

class AiAgentPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Qcai2.json")

public:
    AiAgentPlugin();
    ~AiAgentPlugin() final;

    void initialize() final;
    void extensionsInitialized() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void createDockWidget();
    void setupProviders();
    void registerTools();

    AgentController  *m_controller    = nullptr;
    EditorContext    *m_editorContext  = nullptr;
    ToolRegistry     *m_toolRegistry  = nullptr;
    SafetyPolicy     *m_safetyPolicy  = nullptr;
    IAIProvider      *m_currentProvider = nullptr;

    // Provider instances
    QList<IAIProvider *> m_providers;
    AiCompletionProvider *m_completionProvider = nullptr;
};

} // namespace Internal
} // namespace Qcai2
