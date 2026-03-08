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
struct ValidationResult
{
    /** @brief True when the diff passes all checks. */
    bool valid = false;

    /** @brief Added plus removed hunk lines. */
    int linesChanged = 0;

    /** @brief Number of distinct files referenced by the diff. */
    int filesChanged = 0;

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
ValidationResult validate(const QString &unifiedDiff, int maxLines = 300, int maxFiles = 10);

/**
 * @brief Recomputes hunk counts in unified diff headers.
 * @param unifiedDiff Diff text to normalize.
 * @return Diff text with updated @@ header counts when hunks are present.
 */
QString normalize(const QString &unifiedDiff);

/**
 * @brief Applies a unified diff inside a working directory.
 * @param unifiedDiff Diff text to apply.
 * @param workDir Repository or project directory used as the patch root.
 * @param dryRun When true, runs the patch command without modifying files.
 * @param errorMsg Receives stderr or process error details on failure.
 * @return True when the patch command exits successfully.
 */
bool applyPatch(const QString &unifiedDiff, const QString &workDir, bool dryRun,
                QString &errorMsg);

/**
 * @brief Reverts a previously applied unified diff.
 * @param unifiedDiff Diff text to reverse.
 * @param workDir Repository or project directory used as the patch root.
 * @param errorMsg Receives stderr or process error details on failure.
 * @return True when the reverse patch command exits successfully.
 */
bool revertPatch(const QString &unifiedDiff, const QString &workDir, QString &errorMsg);

/**
 * @brief Describes files created from explicit new-file blocks.
 */
struct NewFileResult
{
    /** @brief Remaining unified diff after new-file blocks are removed. */
    QString remainingDiff;

    /** @brief Relative paths created or that would be created. */
    QStringList createdFiles;

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
NewFileResult extractAndCreateNewFiles(const QString &patchText, const QString &workDir,
                                       bool dryRun = false);

}  // namespace qcai2::Diff
