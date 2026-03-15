/*! Implements plugin setup for providers, tools, completion hooks, and the dock UI. */
#include "AiAgentPlugin.h"
#include "../qcai2constants.h"
#include "../qcai2tr.h"
#include "AgentController.h"
#include "completion/AiCompletionProvider.h"
#include "completion/GhostTextManager.h"
#include "context/ChatContextManager.h"
#include "ui/AgentDockWidget.h"
// CompletionTrigger removed — completion triggers via isActivationCharSequence instead
#include "context/EditorContext.h"
#include "mcp/McpToolManager.h"
#include "providers/CopilotProvider.h"
#include "providers/LocalHttpProvider.h"
#include "providers/OllamaProvider.h"
#include "providers/OpenAICompatibleProvider.h"
#include "safety/SafetyPolicy.h"
#include "settings/Settings.h"
#include "settings/SettingsPage.h"
#include "tools/BuildTools.h"
#include "tools/CreateFileTool.h"
#include "tools/FileTools.h"
#include "tools/FindSymbolTool.h"
#include "tools/GitTools.h"
#include "tools/IdeTools.h"
#include "tools/ListDirectoryTool.h"
#include "tools/RunCommandTool.h"
#include "tools/SearchTools.h"
#include "tools/ToolRegistry.h"
#include "util/CrashHandler.h"
#include "util/IdeOutputCapture.h"
#include "util/Logger.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/inavigationwidgetfactory.h>
#include <coreplugin/navigationwidget.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <QAction>
#include <QMenu>

using namespace Core;

namespace qcai2::Internal
{

class AiAgentNavigationWidgetFactory final : public INavigationWidgetFactory
{
public:
    explicit AiAgentNavigationWidgetFactory(AgentController *controller,
                                            ChatContextManager *chatContextManager,
                                            QObject *parent = nullptr)
        : INavigationWidgetFactory(), m_controller(controller),
          m_chatContextManager(chatContextManager)
    {
        setParent(parent);
        setDisplayName(Tr::tr("AI Agent"));
        setId(Constants::NAVIGATION_ID);
        setPriority(200);
    }

private:
    NavigationView createWidget() override
    {
        auto *widget = new AgentDockWidget(m_controller, m_chatContextManager);
        widget->setObjectName(QStringLiteral("qcai2.AgentNavigationWidget"));
        return {widget, {}};
    }

    AgentController *m_controller = nullptr;
    ChatContextManager *m_chatContextManager = nullptr;
};

AiAgentPlugin::AiAgentPlugin() = default;

AiAgentPlugin::~AiAgentPlugin()
{
    // Providers are QObject children, cleaned up automatically
}

void AiAgentPlugin::initialize()
{
    // Load saved settings
    settings().load();

    // Configure debug logging
    Logger::instance().setEnabled(settings().debugLogging);
    installCrashHandler();
    QCAI_INFO("Plugin", QStringLiteral("qcai2 plugin initializing"));

    // Create components
    m_editorContext = new EditorContext(this);
    m_chatContextManager = new ChatContextManager(this);
    m_toolRegistry = new ToolRegistry(this);
    m_mcpToolManager = new McpToolManager(m_toolRegistry, this);
    m_safetyPolicy = new SafetyPolicy(this);
    m_controller = new AgentController(this);
    m_outputCapture = new IdeOutputCapture(this);

    // Configure safety from settings
    auto &s = settings();
    m_safetyPolicy->setMaxIterations(s.maxIterations);
    m_safetyPolicy->setMaxToolCalls(s.maxToolCalls);
    m_safetyPolicy->setMaxDiffLines(s.maxDiffLines);
    m_safetyPolicy->setMaxChangedFiles(s.maxChangedFiles);

    // Set up tools and providers
    registerTools();
    m_outputCapture->initialize();
    setupProviders();
    QMetaObject::invokeMethod(this, &AiAgentPlugin::refreshCopilotModels, Qt::QueuedConnection);
    QCAI_DEBUG("Plugin", QStringLiteral("Registered %1 tools, %2 providers")
                             .arg(m_toolRegistry->allTools().size())
                             .arg(m_providers.size()));

    // Wire controller
    m_controller->setToolRegistry(m_toolRegistry);
    m_controller->setEditorContext(m_editorContext);
    m_controller->setChatContextManager(m_chatContextManager);
    m_controller->setMcpToolManager(m_mcpToolManager);
    m_controller->setSafetyPolicy(m_safetyPolicy);
    m_navigationWidgetFactory =
        new AiAgentNavigationWidgetFactory(m_controller, m_chatContextManager, this);

    // Set up AI code completion
    m_completionProvider = new AiCompletionProvider(this);
    m_completionProvider->setAiProvider(m_currentProvider);
    m_completionProvider->setChatContextManager(m_chatContextManager);
    m_completionProvider->setModel(s.modelName);
    m_completionProvider->setEnabled(s.aiCompletionEnabled);
    QCAI_DEBUG("Plugin", QStringLiteral("AI completion: %1, model: %2")
                             .arg(s.aiCompletionEnabled ? QStringLiteral("enabled")
                                                        : QStringLiteral("disabled"),
                                  s.completionModel.isEmpty() ? s.modelName : s.completionModel));

    // Set up ghost-text overlay completion
    m_ghostTextManager = new GhostTextManager(this);
    m_ghostTextManager->setProvider(m_currentProvider);
    m_ghostTextManager->setChatContextManager(m_chatContextManager);
    m_ghostTextManager->setModel(s.completionModel.isEmpty() ? s.modelName : s.completionModel);
    m_ghostTextManager->setEnabled(s.aiCompletionEnabled);

    // Attach completion provider to every text editor
    connect(Core::EditorManager::instance(), &Core::EditorManager::editorOpened, this,
            [this](Core::IEditor *editor) {
                if (auto *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor))
                {
                    // Defer attachment to next event loop iteration to let editor fully init
                    QMetaObject::invokeMethod(
                        this,
                        [this, textEditor]() {
                            if (auto *doc = textEditor->textDocument())
                            {
                                doc->setCompletionAssistProvider(m_completionProvider);
                                QCAI_DEBUG("Plugin",
                                           QStringLiteral("Attached AI completion to: %1")
                                               .arg(doc->filePath().toUrlishString()));
                                // Attach ghost-text overlay to the editor widget
                                if (auto *widget = textEditor->editorWidget())
                                {
                                    m_ghostTextManager->attachToEditor(widget);
                                }
                            }
                        },
                        Qt::QueuedConnection);
                }
            });

    // Register settings page
    new SettingsPage;  // auto-registers with Core

    // Add menu action to show the agent sidebar
    ActionContainer *menu = ActionManager::createMenu(Constants::MENU_ID);
    menu->menu()->setTitle(Tr::tr("AI Agent"));
    ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

    ActionBuilder(this, Constants::ACTION_ID)
        .addToContainer(Constants::MENU_ID)
        .setText(Tr::tr("Show AI Agent"))
        .setDefaultKeySequence(Tr::tr("Ctrl+Alt+Meta+A"))
        .addOnTriggered(this, &AiAgentPlugin::showNavigationWidget);
}

void AiAgentPlugin::extensionsInitialized()
{
    // Show the navigation widget on startup
    showNavigationWidget();
}

AiAgentPlugin::ShutdownFlag AiAgentPlugin::aboutToShutdown()
{
    if ((m_controller != nullptr) && m_controller->isRunning())
    {
        m_controller->stop();
    }
    settings().save();
    return SynchronousShutdown;
}

void AiAgentPlugin::showNavigationWidget()
{
    if (ICore::mainWindow() == nullptr)
    {
        QCAI_ERROR("Plugin",
                   QStringLiteral(
                       "Main window not available yet, deferring AI Agent sidebar activation"));
        // Retry later
        QMetaObject::invokeMethod(this, &AiAgentPlugin::showNavigationWidget,
                                  Qt::QueuedConnection);
        return;
    }

    if (NavigationWidget::activateSubWidget(Constants::NAVIGATION_ID, Side::Right) == nullptr)
    {
        QCAI_ERROR("Plugin", QStringLiteral("Failed to activate AI Agent navigation widget"));
        return;
    }

    QCAI_INFO("Plugin", QStringLiteral("AI Agent navigation widget activated"));
}

void AiAgentPlugin::setupProviders()
{
    auto &s = settings();
    QCAI_DEBUG("Plugin", QStringLiteral("Setting up providers, active: %1").arg(s.provider));

    // OpenAI-compatible
    auto *openai = new OpenAICompatibleProvider(this);
    openai->setBaseUrl(s.baseUrl);
    openai->setApiKey(s.apiKey);
    m_providers.append(openai);

    // Local HTTP
    auto *local = new LocalHttpProvider(this);
    local->setBaseUrl(s.localBaseUrl);
    local->setEndpointPath(s.localEndpointPath);
    local->setSimpleMode(s.localSimpleMode);
    local->setApiKey(s.apiKey);
    m_providers.append(local);

    // Ollama
    auto *ollama = new OllamaProvider(this);
    ollama->setBaseUrl(s.ollamaBaseUrl);
    m_providers.append(ollama);

    // GitHub Copilot (Node.js sidecar)
    auto *copilot = new CopilotProvider(this);
    if (!s.copilotNodePath.isEmpty())
    {
        copilot->setNodePath(s.copilotNodePath);
    }
    if (!s.copilotSidecarPath.isEmpty())
    {
        copilot->setSidecarPath(s.copilotSidecarPath);
    }
    m_copilotProvider = copilot;
    m_providers.append(copilot);

    // Select active provider
    m_currentProvider = nullptr;
    for (auto *p : m_providers)
    {
        if (p->id() == s.provider)
        {
            m_currentProvider = p;
            break;
        }
    }
    if ((m_currentProvider == nullptr) && !m_providers.isEmpty())
    {
        m_currentProvider = m_providers.first();
    }

    m_controller->setProvider(m_currentProvider);
    m_controller->setProviders(m_providers);
    QCAI_INFO(
        "Plugin",
        QStringLiteral("Active provider: %1 (%2)")
            .arg(m_currentProvider ? m_currentProvider->id() : QStringLiteral("none"),
                 m_currentProvider ? m_currentProvider->displayName() : QStringLiteral("none")));
}

void AiAgentPlugin::refreshCopilotModels()
{
    if (m_copilotProvider == nullptr)
    {
        return;
    }

    m_copilotProvider->listModels([](const QStringList &models, const QString &error) {
        if (!error.isEmpty())
        {
            QCAI_WARN("Plugin", QStringLiteral("Copilot model refresh failed: %1").arg(error));
            return;
        }

        if (models.isEmpty())
        {
            QCAI_WARN("Plugin", QStringLiteral("Copilot model refresh returned no models"));
            return;
        }

        modelCatalog().setCopilotModels(models);
    });
}

void AiAgentPlugin::registerTools()
{
    m_toolRegistry->registerTool(std::make_shared<ReadFileTool>());
    m_toolRegistry->registerTool(std::make_shared<CreateFileTool>());
    m_toolRegistry->registerTool(std::make_shared<ApplyPatchTool>());
    m_toolRegistry->registerTool(std::make_shared<SearchRepoTool>());
    m_toolRegistry->registerTool(std::make_shared<ListDirectoryTool>());
    m_toolRegistry->registerTool(std::make_shared<RunCommandTool>());
    m_toolRegistry->registerTool(std::make_shared<FindSymbolTool>());

    auto buildTool = std::make_shared<RunBuildTool>();
    auto testsTool = std::make_shared<RunTestsTool>();
    auto diagTool = std::make_shared<ShowDiagnosticsTool>();
    auto compileOutputTool = std::make_shared<ShowCompileOutputTool>();
    auto applicationOutputTool = std::make_shared<ShowApplicationOutputTool>();
    buildTool->setOutputCapture(m_outputCapture);
    diagTool->setOutputCapture(m_outputCapture);
    compileOutputTool->setOutputCapture(m_outputCapture);
    applicationOutputTool->setOutputCapture(m_outputCapture);
    m_toolRegistry->registerTool(buildTool);
    m_toolRegistry->registerTool(testsTool);
    m_toolRegistry->registerTool(diagTool);
    m_toolRegistry->registerTool(compileOutputTool);
    m_toolRegistry->registerTool(applicationOutputTool);

    m_toolRegistry->registerTool(std::make_shared<GitStatusTool>());
    m_toolRegistry->registerTool(std::make_shared<GitDiffTool>());
    m_toolRegistry->registerTool(std::make_shared<OpenFileAtLocationTool>());
}

}  // namespace qcai2::Internal
