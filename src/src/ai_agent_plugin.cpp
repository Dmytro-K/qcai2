/*! Implements plugin setup for providers, tools, completion hooks, and the dock UI. */
#include "ai_agent_plugin.h"
#include "../qcai2constants.h"
#include "../qcai2tr.h"
#include "agent_controller.h"

#if QCAI2_ENABLE_CLANGD
#include "clangd/clangd_service.h"
#endif

#include "completion/ai_completion_provider.h"
#include "completion/ghost_text_manager.h"
#include "context/chat_context_manager.h"
#include "ui/agent_dock_widget.h"
// completion_trigger_t removed — completion triggers via isActivationCharSequence instead
#include "context/editor_context.h"
#include "mcp/mcp_tool_manager.h"
#include "providers/anthropic_provider.h"
#include "providers/copilot_provider.h"
#include "providers/local_http_provider.h"
#include "providers/ollama_provider.h"
#include "providers/open_ai_compatible_provider.h"
#include "safety/safety_policy.h"
#include "settings/settings.h"
#include "settings/settings_page.h"
#include "tools/build_tools.h"
#include "vector_search/vector_search_service.h"

#if QCAI2_ENABLE_CLANGD
#include "tools/clangd_tools.h"
#endif

#include "tools/create_file_tool.h"
#include "tools/file_tools.h"
#include "tools/find_symbol_tool.h"
#include "tools/git_tools.h"
#include "tools/ide_tools.h"
#include "tools/list_directory_tool.h"
#include "tools/run_command_tool.h"
#include "tools/search_tools.h"
#include "tools/tool_registry.h"
#include "tools/vector_search_tools.h"
#include "util/crash_handler.h"
#include "util/ide_output_capture.h"
#include "util/logger.h"
#include "vector_search/text_embedder.h"
#include "vector_search/vector_indexing_manager.h"

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
#include <QMessageBox>

using namespace Core;

namespace qcai2::Internal
{

class ai_agent_navigation_widget_factory_t final : public INavigationWidgetFactory
{
public:
    explicit ai_agent_navigation_widget_factory_t(agent_controller_t *controller,
                                                  chat_context_manager_t *chat_context_manager,
                                                  QObject *parent = nullptr)
        : INavigationWidgetFactory(), controller(controller),
          chat_context_manager(chat_context_manager)
    {
        setParent(parent);
        setDisplayName(tr_t::tr("AI Agent"));
        setId(Constants::navigation_id);
        setPriority(200);
    }

private:
    NavigationView createWidget() override
    {
        auto *widget = new agent_dock_widget_t(this->controller, this->chat_context_manager);
        widget->setObjectName(QStringLiteral("qcai2.AgentNavigationWidget"));
        return {widget, {}};
    }

    agent_controller_t *controller = nullptr;
    chat_context_manager_t *chat_context_manager = nullptr;
};

ai_agent_plugin_t::ai_agent_plugin_t() = default;

ai_agent_plugin_t::~ai_agent_plugin_t()
{
    // Providers are QObject children, cleaned up automatically
}

void ai_agent_plugin_t::initialize()
{
    // Load saved settings
    settings().load();

    // Configure debug logging
    logger_t::instance().set_enabled(settings().debug_logging);
    install_crash_handler();
    QCAI_INFO("Plugin", QStringLiteral("qcai2 plugin initializing"));

    // Create components
    this->editor_context = new editor_context_t(this);
    this->chat_context_manager = new chat_context_manager_t(this);
    this->tool_registry = new tool_registry_t(this);
    this->mcp_tool_manager = new mcp_tool_manager_t(this->tool_registry, this);
    this->safety_policy = new safety_policy_t(this);
    this->controller = new agent_controller_t(this);
    this->output_capture = new ide_output_capture_t(this);
    this->vector_search_service = &qcai2::vector_search_service();
    vector_indexing_manager().initialize(this->editor_context, this->chat_context_manager);

#if QCAI2_ENABLE_CLANGD
    this->clangd_service = new clangd_service_t(this);
#endif

    // Configure safety from settings
    auto &s = settings();
    this->safety_policy->set_max_iterations(s.max_iterations);
    this->safety_policy->set_max_tool_calls(s.max_tool_calls);
    this->safety_policy->set_max_diff_lines(s.max_diff_lines);
    this->safety_policy->set_max_changed_files(s.max_changed_files);
    this->vector_search_service->apply_settings(s);

    QCAI_INFO("Plugin", QStringLiteral("Vector search: %1")
                            .arg(this->vector_search_service->configuration_summary()));

    // Set up tools and providers
    this->register_tools();
    this->output_capture->initialize();
    this->setup_providers();
    QMetaObject::invokeMethod(this, &ai_agent_plugin_t::refresh_copilot_models,
                              Qt::QueuedConnection);
    QCAI_DEBUG("Plugin", QStringLiteral("Registered %1 tools, %2 providers")
                             .arg(this->tool_registry->all_tools().size())
                             .arg(this->providers.size()));

    // Wire controller
    this->controller->set_tool_registry(this->tool_registry);
    this->controller->set_editor_context(this->editor_context);
    this->controller->set_chat_context_manager(this->chat_context_manager);
    this->controller->set_mcp_tool_manager(this->mcp_tool_manager);
    this->controller->set_safety_policy(this->safety_policy);
    this->navigation_widget_factory = new ai_agent_navigation_widget_factory_t(
        this->controller, this->chat_context_manager, this);

    // Set up AI code completion
    this->completion_provider = new ai_completion_provider_t(this);
    this->completion_provider->set_ai_provider(this->current_provider);
    this->completion_provider->set_chat_context_manager(this->chat_context_manager);
    this->completion_provider->set_model(s.model_name);
    this->completion_provider->set_enabled(s.ai_completion_enabled);
    QCAI_DEBUG(
        "Plugin",
        QStringLiteral("AI completion: %1, model: %2")
            .arg(s.ai_completion_enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"),
                 s.completion_model.isEmpty() ? s.model_name : s.completion_model));

    // Set up ghost-text overlay completion
    this->ghost_text_manager = new ghost_text_manager_t(this);
    this->ghost_text_manager->set_provider(this->current_provider);
    this->ghost_text_manager->set_chat_context_manager(this->chat_context_manager);
    this->ghost_text_manager->set_model(s.completion_model.isEmpty() ? s.model_name
                                                                     : s.completion_model);
    this->ghost_text_manager->set_enabled(s.ai_completion_enabled);

    // Attach completion provider to every text editor
    connect(Core::EditorManager::instance(), &Core::EditorManager::editorOpened, this,
            [this](Core::IEditor *editor) {
                if (auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(editor))
                {
                    // Defer attachment to next event loop iteration to let editor fully init
                    QMetaObject::invokeMethod(
                        this,
                        [this, text_editor]() {
                            if (auto *doc = text_editor->textDocument())
                            {
                                doc->setCompletionAssistProvider(this->completion_provider);
                                QCAI_DEBUG("Plugin",
                                           QStringLiteral("Attached AI completion to: %1")
                                               .arg(doc->filePath().toUrlishString()));
                                // Attach ghost-text overlay to the editor widget
                                if (auto *widget = text_editor->editorWidget())
                                {
                                    this->ghost_text_manager->attach_to_editor(widget);
                                }
                            }
                        },
                        Qt::QueuedConnection);
                }
            });

    // Register settings page
    new settings_page_t;  // auto-registers with Core

    // Add menu action to show the agent sidebar
    ActionContainer *menu = ActionManager::createMenu(Constants::menu_id);
    menu->menu()->setTitle(tr_t::tr("AI Agent"));
    ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

    ActionBuilder(this, Constants::action_id)
        .addToContainer(Constants::menu_id)
        .setText(tr_t::tr("Show AI Agent"))
        .setDefaultKeySequence(tr_t::tr("Ctrl+Alt+Meta+A"))
        .addOnTriggered(this, &ai_agent_plugin_t::show_navigation_widget);

    ActionBuilder(this, Constants::focus_goal_action_id)
        .addToContainer(Constants::menu_id)
        .setText(tr_t::tr("Focus AI Agent Goal"))
        .setDefaultKeySequence(tr_t::tr("Ctrl+Shift+Alt+S"))
        .addOnTriggered(this, &ai_agent_plugin_t::focus_goal_input);
}

void ai_agent_plugin_t::extensionsInitialized()
{
    // Show the navigation widget on startup
    this->show_navigation_widget();

    if ((this->vector_search_service != nullptr) && (settings().vector_search_enabled == true))
    {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                QString error;
                if (this->vector_search_service->verify_connection(&error) == false)
                {
                    QCAI_WARN("Plugin",
                              QStringLiteral("Vector search startup check failed: %1").arg(error));
                    QMessageBox::warning(ICore::mainWindow(), tr_t::tr("Vector Search"),
                                         tr_t::tr("Qdrant server is not available: %1\n\n"
                                                  "Vector search functionality is disabled until "
                                                  "the connection succeeds.")
                                             .arg(error));
                    return;
                }
                if (this->vector_search_service->ensure_collection(
                        text_embedder_t::k_embedding_dimensions, &error) == false)
                {
                    QCAI_WARN(
                        "Plugin",
                        QStringLiteral("Vector search collection check failed: %1").arg(error));
                    QMessageBox::warning(ICore::mainWindow(), tr_t::tr("Vector Search"),
                                         tr_t::tr("Qdrant server is not available: %1\n\n"
                                                  "Vector search functionality is disabled until "
                                                  "the connection succeeds.")
                                             .arg(error));
                    return;
                }
                vector_indexing_manager().refresh_from_settings();
            },
            Qt::QueuedConnection);
    }
}

ai_agent_plugin_t::ShutdownFlag ai_agent_plugin_t::aboutToShutdown()
{
    if ((this->controller != nullptr) && this->controller->is_running())
    {
        this->controller->stop();
    }
    settings().save();
    return SynchronousShutdown;
}

void ai_agent_plugin_t::show_navigation_widget()
{
    if (ICore::mainWindow() == nullptr)
    {
        QCAI_ERROR("Plugin",
                   QStringLiteral(
                       "Main window not available yet, deferring AI Agent sidebar activation"));
        // Retry later
        QMetaObject::invokeMethod(this, &ai_agent_plugin_t::show_navigation_widget,
                                  Qt::QueuedConnection);
        return;
    }

    if (NavigationWidget::activateSubWidget(Constants::navigation_id, Side::Right) == nullptr)
    {
        QCAI_ERROR("Plugin", QStringLiteral("Failed to activate AI Agent navigation widget"));
        return;
    }

    QCAI_INFO("Plugin", QStringLiteral("AI Agent navigation widget activated"));
}

void ai_agent_plugin_t::focus_goal_input()
{
    if (ICore::mainWindow() == nullptr)
    {
        QMetaObject::invokeMethod(this, &ai_agent_plugin_t::focus_goal_input,
                                  Qt::QueuedConnection);
        return;
    }

    if (QWidget *widget =
            NavigationWidget::activateSubWidget(Constants::navigation_id, Side::Right))
    {
        if (auto *dock_widget = qobject_cast<agent_dock_widget_t *>(widget))
        {
            QMetaObject::invokeMethod(
                dock_widget, [dock_widget]() { dock_widget->focus_goal_input(); },
                Qt::QueuedConnection);
        }
        else
        {
            QCAI_WARN("Plugin", QStringLiteral("Activated AI Agent widget has unexpected type"));
        }
        return;
    }

    QCAI_ERROR("Plugin", QStringLiteral("Failed to activate AI Agent navigation widget"));
}

void ai_agent_plugin_t::setup_providers()
{
    auto &s = settings();
    QCAI_DEBUG("Plugin", QStringLiteral("Setting up providers, active: %1").arg(s.provider));

    // OpenAI-compatible
    auto *openai = new open_ai_compatible_provider_t(this);
    openai->set_base_url(s.base_url);
    openai->set_api_key(s.api_key);
    this->providers.append(openai);

    // Anthropic API
    auto *anthropic = new anthropic_provider_t(this);
    anthropic->set_base_url(s.base_url);
    anthropic->set_api_key(s.api_key);
    this->providers.append(anthropic);

    // Local HTTP
    auto *local = new local_http_provider_t(this);
    local->set_base_url(s.local_base_url);
    local->set_endpoint_path(s.local_endpoint_path);
    local->set_simple_mode(s.local_simple_mode);
    local->set_api_key(s.api_key);
    this->providers.append(local);

    // Ollama
    auto *ollama = new ollama_provider_t(this);
    ollama->set_base_url(s.ollama_base_url);
    this->providers.append(ollama);

    // GitHub Copilot (Node.js sidecar)
    auto *copilot = new copilot_provider_t(this);
    if (!s.copilot_node_path.isEmpty())
    {
        copilot->set_node_path(s.copilot_node_path);
    }
    if (!s.copilot_sidecar_path.isEmpty())
    {
        copilot->set_sidecar_path(s.copilot_sidecar_path);
    }
    this->copilot_provider = copilot;
    this->providers.append(copilot);

    // Select active provider
    this->current_provider = nullptr;
    for (auto *p : this->providers)
    {
        if (p->id() == s.provider)
        {
            this->current_provider = p;
            break;
        }
    }
    if ((this->current_provider == nullptr) && !this->providers.isEmpty())
    {
        this->current_provider = this->providers.first();
    }

    this->controller->set_provider(this->current_provider);
    this->controller->set_providers(this->providers);
    QCAI_INFO("Plugin", QStringLiteral("Active provider: %1 (%2)")
                            .arg(this->current_provider ? this->current_provider->id()
                                                        : QStringLiteral("none"),
                                 this->current_provider ? this->current_provider->display_name()
                                                        : QStringLiteral("none")));
}

void ai_agent_plugin_t::refresh_copilot_models()
{
    if (this->copilot_provider == nullptr)
    {
        return;
    }

    this->copilot_provider->list_models([](const QStringList &models, const QString &error) {
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

        model_catalog().set_copilot_models(models);
    });
}

void ai_agent_plugin_t::register_tools()
{
    this->tool_registry->register_tool(std::make_shared<read_file_tool_t>());
    this->tool_registry->register_tool(std::make_shared<create_file_tool_t>());
    this->tool_registry->register_tool(std::make_shared<apply_patch_tool_t>());
    this->tool_registry->register_tool(std::make_shared<search_repo_tool_t>());
    this->tool_registry->register_tool(std::make_shared<vector_search_tool_t>());
    this->tool_registry->register_tool(std::make_shared<vector_search_history_tool_t>());
    this->tool_registry->register_tool(std::make_shared<list_directory_tool_t>());
    this->tool_registry->register_tool(std::make_shared<run_command_tool_t>());
    this->tool_registry->register_tool(std::make_shared<find_symbol_tool_t>());

#if QCAI2_ENABLE_CLANGD
    register_clangd_query_tools(this->tool_registry, this->clangd_service);
#endif

    auto build_tool = std::make_shared<run_build_tool_t>();
    auto tests_tool = std::make_shared<run_tests_tool_t>();
    auto diag_tool = std::make_shared<show_diagnostics_tool_t>();
    auto compile_output_tool = std::make_shared<show_compile_output_tool_t>();
    auto application_output_tool = std::make_shared<show_application_output_tool_t>();
    build_tool->set_output_capture(this->output_capture);
    diag_tool->set_output_capture(this->output_capture);
    compile_output_tool->set_output_capture(this->output_capture);
    application_output_tool->set_output_capture(this->output_capture);
    this->tool_registry->register_tool(build_tool);
    this->tool_registry->register_tool(tests_tool);
    this->tool_registry->register_tool(diag_tool);
    this->tool_registry->register_tool(compile_output_tool);
    this->tool_registry->register_tool(application_output_tool);

    this->tool_registry->register_tool(std::make_shared<git_status_tool_t>());
    this->tool_registry->register_tool(std::make_shared<git_diff_tool_t>());
    this->tool_registry->register_tool(std::make_shared<open_file_at_location_tool_t>());
}

}  // namespace qcai2::Internal
