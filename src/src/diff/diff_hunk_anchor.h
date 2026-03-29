/*! @file
    @brief Declares helpers that compute stable editor anchor lines for unified diff hunks.
*/
#pragma once

#include <QString>

namespace qcai2
{

/**
 * @brief Returns the 1-based old-document line where the first real hunk change starts.
 * @param start_line_old First old-document line from the @@ header.
 * @param start_line_new First new-document line from the @@ header.
 * @param hunk_body Unified diff hunk body without the @@ header line.
 * @return Best-effort 1-based anchor line for inline diff UI placement.
 */
int first_change_old_line_for_hunk(int start_line_old, int start_line_new,
                                   const QString &hunk_body);

}  // namespace qcai2
