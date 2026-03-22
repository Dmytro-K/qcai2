/*! Resolves project roots for file paths using Qt Creator project metadata when available. */
#pragma once

#include <QString>

namespace qcai2
{

QString project_root_for_file_path(const QString &file_path);

}  // namespace qcai2
