/*! Migration steps for revision 0.0.7-001. */

#pragma once

#include <QJsonObject>
#include <QString>

namespace qcai2::Migration
{

bool project_state_needs_migration_to_0_0_7_001(const QString &storage_path,
                                                const QJsonObject &root);
bool normalize_conversation_log_storage_to_0_0_7_001(const QString &storage_path,
                                                     QString *error = nullptr);

bool migrate_project_state_to_0_0_7_001(const QString &storage_path, QJsonObject &root,
                                        QString *error = nullptr);

}  // namespace qcai2::Migration
