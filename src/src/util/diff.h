#pragma once

/*! @file
    @brief Utilities for validating and applying unified diffs.
*/

#include <QString>
#include <QStringList>

namespace qcai2::Diff
{

/**
 * @brief Describes the outcome of unified diff validation.
 */
struct validation_result_t
{
    /** @brief True when the diff passes all checks. */
    bool valid = false;

    /** @brief Added plus removed hunk lines. */
    int lines_changed = 0;

    /** @brief Number of distinct files referenced by the diff. */
    int files_changed = 0;

    /** @brief Validation failure message when @c valid is false. */
    QString error;
};

/**
 * @brief Validates a unified diff and enforces repository limits.
 * @param unifiedDiff Diff text to inspect.
 * @param maxLines Maximum number of added and removed lines allowed.
 * @param maxFiles Maximum number of changed files allowed.
 * @return Validation details including counters and any error message.
 */
validation_result_t validate(const QString &unified_diff, int max_lines = 300, int max_files = 10);

/**
 * @brief Recomputes hunk counts in unified diff headers.
 * @param unifiedDiff Diff text to normalize.
 * @return Diff text with updated @@ header counts when hunks are present.
 */
QString normalize(const QString &unified_diff);

/**
 * @brief Applies a unified diff inside a working directory.
 * @param unifiedDiff Diff text to apply.
 * @param workDir Repository or project directory used as the patch root.
 * @param dryRun When true, runs the patch command without modifying files.
 * @param errorMsg Receives stderr or process error details on failure.
 * @return True when the patch command exits successfully.
 */
bool apply_patch(const QString &unified_diff, const QString &work_dir, bool dry_run,
                 QString &error_msg);

/**
 * @brief Reverts a previously applied unified diff.
 * @param unifiedDiff Diff text to reverse.
 * @param workDir Repository or project directory used as the patch root.
 * @param errorMsg Receives stderr or process error details on failure.
 * @return True when the reverse patch command exits successfully.
 */
bool revert_patch(const QString &unified_diff, const QString &work_dir, QString &error_msg);

/**
 * @brief Describes files created from explicit new-file blocks.
 */
struct new_file_result_t
{
    /** @brief Remaining unified diff after new-file blocks are removed. */
    QString remaining_diff;

    /** @brief Relative paths created or that would be created. */
    QStringList created_files;

    /** @brief Error message when extraction or creation fails. */
    QString error;
};

/**
 * @brief Creates files described by custom "NEW FILE" sections.
 * @param patchText Mixed patch text that may contain file creation blocks.
 * @param workDir Project directory used to resolve relative file paths.
 * @param dryRun When true, validates paths without writing files.
 * @return Created-file details plus the remaining unified diff text.
 */
new_file_result_t extract_and_create_new_files(const QString &patch_text, const QString &work_dir,
                                               bool dry_run = false);

}  // namespace qcai2::Diff
