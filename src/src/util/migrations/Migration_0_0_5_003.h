/*! Migration steps for revision 0.0.5-003. */
#pragma once

#include <QJsonObject>
#include <QSettings>

namespace qcai2::Migration
{

bool migrateGlobalSettingsTo_0_0_5_003(QSettings &settings);
bool migrateProjectStateTo_0_0_5_003(QJsonObject &root);

}  // namespace qcai2::Migration
