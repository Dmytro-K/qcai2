/*! @file
    @brief Declares helpers that map Diff Preview lines to editor navigation targets.
*/
#pragma once

#include <QHash>
#include <QString>

namespace qcai2
{

/*! @brief Describes the editor location associated with one rendered Diff Preview line. */
struct diff_preview_navigation_target_t
{
    QString absolute_file_path;
    int line = 0;

    /*! @brief Returns true when the target can be opened in the editor. */
    [[nodiscard]] bool is_valid() const;
};

/*! @brief Builds a 1-based preview-line map for unified diff navigation. */
QHash<int, diff_preview_navigation_target_t>
build_diff_preview_navigation_targets(const QString &diff_text, const QString &project_dir);

}  // namespace qcai2
