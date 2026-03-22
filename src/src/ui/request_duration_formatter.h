/*! @file
    @brief Declares formatting helpers for the live request duration label.
*/
#pragma once

#include <QString>

namespace qcai2
{

QString format_request_duration_label(qint64 elapsed_ms);

}  // namespace qcai2
