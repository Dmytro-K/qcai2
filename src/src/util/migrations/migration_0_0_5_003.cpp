/*! Migration steps for revision 0.0.5-003. */

#include "migration_0_0_5_003.h"

namespace qcai2::Migration
{

bool migrate_global_settings_to_0_0_5_003(QSettings &settings)
{
    Q_UNUSED(settings);
    return false;
}

bool migrate_project_state_to_0_0_5_003(QJsonObject &root)
{
    Q_UNUSED(root);
    return false;
}

}  // namespace qcai2::Migration
