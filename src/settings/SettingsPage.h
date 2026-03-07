#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

namespace qcai2
{

// Qt Creator Options page: Tools > Options > AI Agent
class SettingsPage : public Core::IOptionsPage
{
public:
    SettingsPage();
};

}  // namespace qcai2
