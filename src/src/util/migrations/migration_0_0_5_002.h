/*! Migration steps for revision 0.0.5-002. */
#pragma once

#include <QJsonObject>
#include <QString>

namespace qcai2::Migration
{

bool migrate_project_state_to_0_0_5_002(QJsonObject &root);

}  // namespace qcai2::Migration
