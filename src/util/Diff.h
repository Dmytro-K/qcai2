#pragma once

#include <QString>
#include <QStringList>

namespace qcai2::Diff
{

struct ValidationResult
{
    bool valid = false;
    int linesChanged = 0;
    int filesChanged = 0;
    QString error;
};

// Validate a unified diff string.
// Checks syntax and enforces size limits.
// maxLines:  max total ±lines across all hunks  (default 300)
// maxFiles:  max number of changed files        (default 10)
ValidationResult validate(const QString &unifiedDiff, int maxLines = 300, int maxFiles = 10);

// Normalize unified diff hunks by recalculating line counters in @@ headers.
// Returns input unchanged when no hunk headers are found.
QString normalize(const QString &unifiedDiff);

// Apply a unified diff using "patch" command inside workDir.
// Returns true on success, errorMsg on failure.
bool applyPatch(const QString &unifiedDiff, const QString &workDir, bool dryRun,
                QString &errorMsg);

// Revert a previously applied patch (patch -R).
bool revertPatch(const QString &unifiedDiff, const QString &workDir, QString &errorMsg);

// Extract "=== NEW FILE: path ===" blocks from mixed patch text.
// Creates files on disk (unless dryRun), returns the remaining unified diff
// and the list of created file paths.
struct NewFileResult
{
    QString remainingDiff;
    QStringList createdFiles;
    QString error;
};
NewFileResult extractAndCreateNewFiles(const QString &patchText, const QString &workDir,
                                       bool dryRun = false);

}  // namespace qcai2::Diff
