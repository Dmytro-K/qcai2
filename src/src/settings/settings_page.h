#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

namespace qcai2
{

/**
 * Registers the plugin settings page in Qt Creator options.
 */
class settings_page_t : public Core::IOptionsPage
{
public:
    /**
     * Creates and registers the settings page metadata.
     */
    settings_page_t();
};

}  // namespace qcai2
