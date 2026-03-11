/*! Migration steps for revision 0.0.4-001. */
#pragma once

#include <QJsonObject>
#include <QSettings>
#include <QString>

namespace qcai2::Migration
{

bool migrateGlobalSettingsTo_0_0_4_001(QSettings &settings);
bool migrateProjectStateTo_0_0_4_001(const QString &storagePath, QJsonObject &root, QString *error);

}  // namespace qcai2::Migration
