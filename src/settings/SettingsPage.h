#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

namespace qcai2
{

/**
 * Registers the plugin settings page in Qt Creator options.
 */
class SettingsPage : public Core::IOptionsPage
{
public:
    /**
     * Creates and registers the settings page metadata.
     */
    SettingsPage();

};

}  // namespace qcai2
