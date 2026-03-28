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
#include "debugger/debugger_session_service.h"
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
#include "tools/debugger_tools.h"
#include "tools/file_tools.h"
#include "tools/find_symbol_tool.h"
#include "tools/git_tools.h"
#include "tools/ide_tools.h"
#include "tools/list_directory_tool.h"
#include "tools/run_command_tool.h"
#include "tools/search_tools.h"
#include "tools/tool_registry.h"
#include "tools/vector_search_tools.h"
#include "tools/web_tools.h"
#include "util/crash_handler.h"
#include "util/ide_output_capture.h"
#include "util/logger.h"
#include "util/project_root_resolver.h"
#include "util/system_diagnostics_log.h"
#include "vector_search/text_embedder.h"
#include "vector_search/vector_indexing_manager.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/inavigationwidgetfactory.h>
#include <coreplugin/navigationwidget.h>
#include <texteditor/codeassist/assistenums.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <QAction>
#include <QDateTime>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

using namespace Core;

namespace qcai2::Internal
{

namespace
{

QList<iai_provider_t *> create_provider_pool(QObject *owner, const settings_t &settings,
                                             copilot_provider_t **copilot_provider_out = nullptr)
{
    QList<iai_provider_t *> providers;

    auto *openai = new open_ai_compatible_provider_t(owner);
    openai->set_base_url(settings.base_url);
    openai->set_api_key(settings.api_key);
    providers.append(openai);

    auto *anthropic = new anthropic_provider_t(owner);
    anthropic->set_base_url(settings.base_url);
    anthropic->set_api_key(settings.api_key);
    providers.append(anthropic);

    auto *local = new local_http_provider_t(owner);
    local->set_base_url(settings.local_base_url);
    local->set_endpoint_path(settings.local_endpoint_path);
    local->set_simple_mode(settings.local_simple_mode);
    local->set_api_key(settings.api_key);
    providers.append(local);

    auto *ollama = new ollama_provider_t(owner);
    ollama->set_base_url(settings.ollama_base_url);
    providers.append(ollama);

    auto *copilot = new copilot_provider_t(owner);
    if (settings.copilot_node_path.isEmpty() == false)
    {
        copilot->set_node_path(settings.copilot_node_path);
    }
    if (settings.copilot_sidecar_path.isEmpty() == false)
    {
        copilot->set_sidecar_path(settings.copilot_sidecar_path);
    }
    providers.append(copilot);
    if (copilot_provider_out != nullptr)
    {
        *copilot_provider_out = copilot;
    }

    return providers;
}

iai_provider_t *find_provider_by_id(const QList<iai_provider_t *> &providers, const QString &id)
{
    for (iai_provider_t *provider : providers)
    {
        if (provider != nullptr && provider->id() == id)
        {
            return provider;
        }
    }
    return nullptr;
}

QString current_plugin_module_path()
{
#if defined(_WIN32)
    HMODULE module_handle = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&current_plugin_module_path), &module_handle) == 0 ||
        module_handle == nullptr)
    {
        return {};
    }

    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module_handle, buffer, MAX_PATH);
    if (length == 0)
    {
        return {};
    }
    return QString::fromWCharArray(buffer, static_cast<int>(length));
#elif defined(__unix__) || defined(__APPLE__)
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&current_plugin_module_path), &info) == 0 ||
        info.dli_fname == nullptr)
    {
        return {};
    }
    return QString::fromLocal8Bit(info.dli_fname);
#else
    return {};
#endif
}

QString startup_workspace_root(editor_context_t *editor_context,
                               chat_context_manager_t *chat_context_manager)
{
    if (chat_context_manager != nullptr)
    {
        const QString active_workspace_root = chat_context_manager->active_workspace_root();
        if (active_workspace_root.isEmpty() == false)
        {
            return active_workspace_root;
        }
    }
    if (editor_context == nullptr)
    {
        return {};
    }

    const editor_context_t::snapshot_t snapshot = editor_context->capture();
    if (snapshot.project_dir.isEmpty() == false)
    {
        return snapshot.project_dir;
    }
    if (snapshot.file_path.isEmpty() == false)
    {
        return project_root_for_file_path(snapshot.file_path);
    }

    const QList<editor_context_t::project_info_t> open_projects = editor_context->open_projects();
    for (const editor_context_t::project_info_t &project : open_projects)
    {
        if (project.project_dir.isEmpty() == false)
        {
            return project.project_dir;
        }
    }

    return {};
}

}  // namespace

class ai_agent_navigation_widget_factory_t final : public INavigationWidgetFactory
{
public:
    explicit ai_agent_navigation_widget_factory_t(agent_controller_t *controller,
                                                  chat_context_manager_t *chat_context_manager,
                                                  debugger_session_service_t *debugger_service,
                                                  QObject *parent = nullptr)
        : INavigationWidgetFactory(), controller(controller),
          chat_context_manager(chat_context_manager), debugger_service(debugger_service)
    {
        setParent(parent);
        setDisplayName(tr_t::tr("AI Agent"));
        setId(Constants::navigation_id);
        setPriority(200);
    }

private:
    NavigationView createWidget() override
    {
        auto *widget = new agent_dock_widget_t(this->controller, this->chat_context_manager,
                                               this->debugger_service);
        widget->setObjectName(QStringLiteral("qcai2.AgentNavigationWidget"));
        return {widget, {}};
    }

    agent_controller_t *controller = nullptr;
    chat_context_manager_t *chat_context_manager = nullptr;
    debugger_session_service_t *debugger_service = nullptr;
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
    connect(this->chat_context_manager, &chat_context_manager_t::active_workspace_changed, this,
            [this](const QString &, const QString &, const QString &) {
                this->log_startup_diagnostics();
            });
    this->tool_registry = new tool_registry_t(this);
    this->mcp_tool_manager = new mcp_tool_manager_t(this->tool_registry, this);
    this->safety_policy = new safety_policy_t(this);
    this->controller = new agent_controller_t(this);
    this->output_capture = new ide_output_capture_t(this);
    this->debugger_service = new debugger_session_service_t(this);
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
    this->output_capture->set_debugger_service(this->debugger_service);
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
        this->controller, this->chat_context_manager, this->debugger_service, this);

    // Set up AI code completion
    this->completion_provider = new ai_completion_provider_t(this);
    this->completion_provider->set_providers(this->completion_providers);
    this->completion_provider->set_chat_context_manager(this->chat_context_manager);
    this->completion_provider->set_clangd_service(this->clangd_service);
    this->completion_provider->set_model(s.model_name);
    this->completion_provider->set_enabled(true);
    QCAI_DEBUG(
        "Plugin",
        QStringLiteral("AI completion: %1, model: %2")
            .arg(s.ai_completion_enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"),
                 s.completion_model.isEmpty() ? s.model_name : s.completion_model));

    // Set up ghost-text overlay completion
    this->ghost_text_manager = new ghost_text_manager_t(this);
    this->ghost_text_manager->set_providers(this->completion_providers);
    this->ghost_text_manager->set_chat_context_manager(this->chat_context_manager);
    this->ghost_text_manager->set_clangd_service(this->clangd_service);
    this->ghost_text_manager->set_model(s.completion_model.isEmpty() ? s.model_name
                                                                     : s.completion_model);
    this->ghost_text_manager->set_enabled(true);

    // Attach completion provider to every text editor
    connect(
        Core::EditorManager::instance(), &Core::EditorManager::editorOpened, this,
        [this](Core::IEditor *editor) {
            if (auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(editor))
            {
                // Defer attachment to next event loop iteration to let editor fully init
                QMetaObject::invokeMethod(
                    this,
                    [this, text_editor]() { this->attach_completion_to_text_editor(text_editor); },
                    Qt::QueuedConnection);
            }
        });
    QMetaObject::invokeMethod(
        this,
        [this]() {
            const QList<Core::IEditor *> visible_editors = Core::EditorManager::visibleEditors();
            for (Core::IEditor *editor : visible_editors)
            {
                if (auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(editor))
                {
                    this->attach_completion_to_text_editor(text_editor);
                }
            }
            if (Core::IEditor *current_editor = Core::EditorManager::currentEditor())
            {
                if (auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(current_editor))
                {
                    this->attach_completion_to_text_editor(text_editor);
                }
            }
        },
        Qt::QueuedConnection);

    // Register settings page
    new settings_page_t;  // auto-registers with Core

    // Add menu action to show the agent sidebar
    ActionContainer *menu = ActionManager::createMenu(Constants::menu_id);
    menu->menu()->setTitle(tr_t::tr("Qcai2"));
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

    ActionBuilder(this, Constants::queue_request_action_id)
        .addToContainer(Constants::menu_id)
        .setText(tr_t::tr("Queue AI Agent Request"))
        .setDefaultKeySequence(tr_t::tr("Ctrl+Enter"))
        .addOnTriggered(this, &ai_agent_plugin_t::queue_current_request);

    ActionBuilder(this, Constants::trigger_completion_action_id)
        .addToContainer(Constants::menu_id)
        .setText(tr_t::tr("Trigger qcai2 Autocomplete"))
        .setDefaultKeySequence(tr_t::tr("Ctrl+Shift+Space"))
        .addOnTriggered(this, &ai_agent_plugin_t::trigger_completion);
}

void ai_agent_plugin_t::extensionsInitialized()
{
    this->log_startup_diagnostics();

    // Show the navigation widget on startup
    this->show_navigation_widget();

#if QCAI2_FEATURE_QDRANT_ENABLE
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
                        text_embedder_t::embedding_dimensions, &error) == false)
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
#endif
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

    this->with_agent_dock_widget([](agent_dock_widget_t *dock_widget) {
        QMetaObject::invokeMethod(
            dock_widget, [dock_widget]() { dock_widget->focus_goal_input(); },
            Qt::QueuedConnection);
    });
}

void ai_agent_plugin_t::queue_current_request()
{
    if (ICore::mainWindow() == nullptr)
    {
        QMetaObject::invokeMethod(this, &ai_agent_plugin_t::queue_current_request,
                                  Qt::QueuedConnection);
        return;
    }

    this->with_agent_dock_widget([](agent_dock_widget_t *dock_widget) {
        QMetaObject::invokeMethod(
            dock_widget, [dock_widget]() { dock_widget->queue_current_request(); },
            Qt::QueuedConnection);
    });
}

void ai_agent_plugin_t::trigger_completion()
{
    if (this->completion_provider == nullptr)
    {
        QCAI_WARN("Plugin",
                  QStringLiteral("Cannot trigger autocomplete: completion provider missing"));
        return;
    }

    auto &s = settings();
    if (s.ai_completion_enabled == false)
    {
        QCAI_INFO("Plugin",
                  QStringLiteral("Skipping manual autocomplete trigger: completion disabled"));
        return;
    }

    Core::IEditor *current_editor = Core::EditorManager::currentEditor();
    auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(current_editor);
    if (text_editor == nullptr)
    {
        QCAI_INFO(
            "Plugin",
            QStringLiteral(
                "Skipping manual autocomplete trigger: current editor is not a text editor"));
        return;
    }

    TextEditor::TextDocument *text_document = text_editor->textDocument();
    TextEditor::TextEditorWidget *editor_widget = text_editor->editorWidget();
    if ((text_document == nullptr) || (editor_widget == nullptr))
    {
        QCAI_WARN("Plugin",
                  QStringLiteral(
                      "Cannot trigger autocomplete: text editor is missing document or widget"));
        return;
    }

    text_document->setCompletionAssistProvider(this->completion_provider);
    editor_widget->invokeAssist(TextEditor::Completion, this->completion_provider);
}

void ai_agent_plugin_t::with_agent_dock_widget(
    const std::function<void(agent_dock_widget_t *)> &callback)
{
    if (QWidget *widget =
            NavigationWidget::activateSubWidget(Constants::navigation_id, Side::Right))
    {
        if (auto *dock_widget = qobject_cast<agent_dock_widget_t *>(widget))
        {
            callback(dock_widget);
        }
        else
        {
            QCAI_WARN("Plugin", QStringLiteral("Activated AI Agent widget has unexpected type"));
        }
        return;
    }

    QCAI_ERROR("Plugin", QStringLiteral("Failed to activate AI Agent navigation widget"));
}

void ai_agent_plugin_t::attach_completion_to_text_editor(TextEditor::BaseTextEditor *text_editor)
{
    if (text_editor == nullptr)
    {
        return;
    }
    if (auto *doc = text_editor->textDocument())
    {
        doc->setCompletionAssistProvider(this->completion_provider);
        const QString file_path = doc->filePath().toUrlishString();
        const QString workspace_root =
            startup_workspace_root(this->editor_context, this->chat_context_manager);
        if (workspace_root.isEmpty() == false)
        {
            append_system_diagnostics_event(
                workspace_root, QStringLiteral("Plugin"), QStringLiteral("completion.wired"),
                QStringLiteral("Wired qcai2 completion services to a text editor."),
                {QStringLiteral("file=%1").arg(
                     file_path.isEmpty() == false ? file_path : QStringLiteral("<untitled>")),
                 QStringLiteral("completion_provider=%1")
                     .arg(this->completion_provider != nullptr ? QStringLiteral("available")
                                                               : QStringLiteral("missing")),
                 QStringLiteral("editor_widget=%1")
                     .arg(text_editor->editorWidget() != nullptr ? QStringLiteral("available")
                                                                 : QStringLiteral("missing"))});
        }
        QCAI_DEBUG(
            "Plugin",
            QStringLiteral("Attached AI completion to: %1").arg(doc->filePath().toUrlishString()));
        if (auto *widget = text_editor->editorWidget())
        {
            this->ghost_text_manager->attach_to_editor(widget);
        }
    }
}

void ai_agent_plugin_t::log_startup_diagnostics()
{
    const QString workspace_root =
        startup_workspace_root(this->editor_context, this->chat_context_manager);
    if (workspace_root.isEmpty() == true)
    {
        return;
    }
    if (this->startup_diagnostics_logged_workspace_roots.contains(workspace_root) == true)
    {
        return;
    }

    const QString module_path = current_plugin_module_path();
    const QFileInfo module_info(module_path);
    const editor_context_t::snapshot_t snapshot = this->editor_context != nullptr
                                                      ? this->editor_context->capture()
                                                      : editor_context_t::snapshot_t{};

    if (append_system_diagnostics_event(
            workspace_root, QStringLiteral("Plugin"), QStringLiteral("startup.build_info"),
            QStringLiteral("qcai2 plugin startup diagnostics."),
            {QStringLiteral("version=%1").arg(QString::fromLatin1(QCAI2_CURRENT_VERSION)),
             QStringLiteral("build_suffix=%1")
                 .arg(QString::fromLatin1(QCAI2_CURRENT_BUILD_SUFFIX)),
             QStringLiteral("compile_timestamp=%1")
                 .arg(QString::fromLatin1(__DATE__ " " __TIME__)),
             QStringLiteral("qtcreator_ide_version=%1")
                 .arg(QString::fromLatin1(QCAI2_QTCREATOR_IDE_VERSION)),
             QStringLiteral("module_path=%1")
                 .arg(module_path.isEmpty() == false ? module_path : QStringLiteral("<unknown>")),
             QStringLiteral("module_last_modified=%1")
                 .arg(module_info.exists() == true
                          ? module_info.lastModified().toString(Qt::ISODateWithMs)
                          : QStringLiteral("<unknown>")),
             QStringLiteral("workspace_root=%1").arg(workspace_root),
             QStringLiteral("project_file=%1")
                 .arg(snapshot.project_file_path.isEmpty() == false
                          ? snapshot.project_file_path
                          : QStringLiteral("<none>"))}) == true)
    {
        this->startup_diagnostics_logged_workspace_roots.insert(workspace_root);
    }
}

void ai_agent_plugin_t::setup_providers()
{
    auto &s = settings();
    QCAI_DEBUG("Plugin", QStringLiteral("Setting up providers, active: %1").arg(s.provider));

    this->providers = create_provider_pool(this, s, &this->copilot_provider);
    this->completion_providers = create_provider_pool(this, s);

    // Select active provider
    this->current_provider = find_provider_by_id(this->providers, s.provider);
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
    this->tool_registry->register_tool(std::make_shared<web_search_tool_t>());
    this->tool_registry->register_tool(std::make_shared<web_fetch_tool_t>());
    this->tool_registry->register_tool(std::make_shared<vector_search_tool_t>());
    this->tool_registry->register_tool(std::make_shared<vector_search_history_tool_t>());
    this->tool_registry->register_tool(std::make_shared<list_directory_tool_t>());
    this->tool_registry->register_tool(std::make_shared<run_command_tool_t>());
    this->tool_registry->register_tool(std::make_shared<find_symbol_tool_t>());
    register_debugger_tools(this->tool_registry, this->debugger_service);

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
