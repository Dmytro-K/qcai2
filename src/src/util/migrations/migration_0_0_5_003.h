/*! Migration steps for revision 0.0.5-003. */
#pragma once

#include <QJsonObject>
#include <QSettings>

namespace qcai2::Migration
{

bool migrate_global_settings_to_0_0_5_003(QSettings &settings);
bool migrate_project_state_to_0_0_5_003(QJsonObject &root);

}  // namespace qcai2::Migration
