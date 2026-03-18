/*! Implements the legacy sample plugin entry point generated for Qcai2. */
#include "qcai2constants.h"
#include "qcai2tr.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>

using namespace Core;

namespace Qcai2::Internal
{

/** Minimal sample plugin that exposes one placeholder Tools action. */
class Qcai2Plugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Qcai2.json")

public:
    Qcai2Plugin() = default;

    ~Qcai2Plugin() final
    {
        // Unregister objects from the plugin manager's object pool
        // Other cleanup, if needed.
    }

    /**
     * Registers the sample menu and action.
     */
    void initialize() final
    {
        // Set up this plugin's factories, if needed.
        // Register objects in the plugin manager's object pool, if needed. (rare)
        // Load settings
        // Add actions to menus
        // Connect to other plugins' signals
        // In the initialize function, a plugin can be sure that the plugins it
        // depends on have passed their initialize() phase.

        // If you need access to command line arguments or to report errors, use the
        //    Utils::result_t<> IPlugin::initialize(const QStringList &arguments)
        // overload.

        ActionContainer *menu = ActionManager::createMenu(Constants::MENU_ID);
        menu->menu()->setTitle(tr_t::tr("Qcai2"));
        ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

        ActionBuilder(this, Constants::ACTION_ID)
            .addToContainer(Constants::MENU_ID)
            .setText(tr_t::tr("Qcai2 Action"))
            .setDefaultKeySequence(tr_t::tr("Ctrl+Alt+Meta+A"))
            .addOnTriggered(this, &Qcai2Plugin::triggerAction);
    }

    /**
     * Completes startup after dependent plugins initialize.
     */
    void extensionsInitialized() final
    {
        // Retrieve objects from the plugin manager's object pool, if needed. (rare)
        // In the extensionsInitialized function, a plugin can be sure that all
        // plugins that depend on it have passed their initialize() and
        // extensionsInitialized() phase.
    }

    /**
     * Returns synchronous shutdown because the sample plugin has no background work.
     */
    ShutdownFlag aboutToShutdown() final
    {
        // Save settings
        // Disconnect from signals that are not needed during shutdown
        // Hide UI (if you add UI that is not in the main window directly)
        return SynchronousShutdown;
    }

private:
    /**
     * Shows the placeholder action dialog.
     */
    void triggerAction()
    {
        QMessageBox::information(ICore::dialogParent(), tr_t::tr("Action Triggered"),
                                 tr_t::tr("This is an action from Qcai2."));
    }
};

}  // namespace Qcai2::Internal

#include <qcai2.moc>
