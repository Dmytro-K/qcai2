/*! Migration steps for revision 0.0.4-001. */
#pragma once

#include <QJsonObject>
#include <QSettings>
#include <QString>

namespace qcai2::Migration
{

bool migrate_global_settings_to_0_0_4_001(QSettings &settings);
bool migrate_project_state_to_0_0_4_001(const QString &storage_path, QJsonObject &root,
                                        QString *error);

}  // namespace qcai2::Migration
