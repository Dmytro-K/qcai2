/*! @file
    @brief Declares per-launch Markdown system diagnostics logging helpers.
*/
#pragma once

#include <QString>
#include <QStringList>

namespace qcai2
{

/**
 * Appends one per-launch diagnostics event into `.qcai2/logs/system-*.md`.
 * @param workspace_root Project/workspace directory used for `.qcai2/logs`.
 * @param component High-level subsystem name, for example `Completion`.
 * @param event Event identifier.
 * @param message Optional human-readable message.
 * @param details Optional detail lines appended as list items.
 * @param error Output error text on failure.
 * @return True on success.
 */
bool append_system_diagnostics_event(const QString &workspace_root, const QString &component,
                                     const QString &event, const QString &message = {},
                                     const QStringList &details = {}, QString *error = nullptr);

/**
 * Returns the current per-launch system diagnostics log file path.
 * @param workspace_root Project/workspace directory used for `.qcai2/logs`.
 * @return Absolute path to `.qcai2/logs/system-*.md`, or empty when unavailable.
 */
QString system_diagnostics_log_file_path(const QString &workspace_root);

}  // namespace qcai2
