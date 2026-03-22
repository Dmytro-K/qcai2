/*! @file
    @brief Implements formatting helpers for the live request duration label.
*/

#include "request_duration_formatter.h"

#include <QCoreApplication>

namespace qcai2
{

QString format_request_duration_label(qint64 elapsed_ms)
{
    const qint64 elapsed_seconds = qMax<qint64>(0, elapsed_ms) / 1000;
    return QCoreApplication::translate("agent_dock_widget_t", "%1 s").arg(elapsed_seconds);
}

}  // namespace qcai2
