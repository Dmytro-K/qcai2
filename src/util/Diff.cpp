/*! @file
    @brief Implements unified diff normalization, validation, and patch helpers.
*/

#include "Diff.h"
#include "ProcessRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace qcai2::Diff
{

/**
 * @brief Runs the patch executable with a normalized diff payload.
 * @param unifiedDiff Diff text passed on stdin to @c patch.
 * @param workDir Working directory used as the patch root.
 * @param extraArgs Additional patch command arguments such as @c --dry-run.
 * @param errorMsg Receives stderr or process error text on failure.
 * @return True when the patch command exits with code zero.
 */
static bool runPatchCmd(const QString &unifiedDiff, const QString &workDir,
                        const QStringList &extraArgs, QString &errorMsg);

QString normalize(const QString &unifiedDiff)
{
    const QStringList inputLines = unifiedDiff.split('\n');
    QStringList lines = inputLines;

    static const QRegularExpression hunkHeader(
        R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(.*)$)");

    for (int i = 0; i < lines.size();)
    {
        const auto m = hunkHeader.match(lines[i]);
        if (!m.hasMatch())
        {
            ++i;
            continue;
        }

        int oldCount = 0;
        int newCount = 0;
        int j = i + 1;
        for (; j < lines.size(); ++j)
        {
            const QString &l = lines[j];
            if (l.startsWith("@@ -") || l.startsWith("--- ") || l.startsWith("diff --git ") ||
                l.startsWith("Index: "))
                break;
            if (l.startsWith("\\ No newline at end of file"))
                continue;
            if (l.startsWith('+') && !l.startsWith("+++ "))
            {
                ++newCount;
                continue;
            }
            if (l.startsWith('-') && !l.startsWith("--- "))
            {
                ++oldCount;
                continue;
            }
            if (l.startsWith(' '))
            {
                ++oldCount;
                ++newCount;
                continue;
            }
        }

        const QString oldCountPart =
            (oldCount == 1) ? QString() : QStringLiteral(",%1").arg(oldCount);
        const QString newCountPart =
            (newCount == 1) ? QString() : QStringLiteral(",%1").arg(newCount);

        lines[i] =
            QStringLiteral("@@ -%1%2 +%3%4 @@%5")
                .arg(m.captured(1), oldCountPart, m.captured(3), newCountPart, m.captured(5));
        i = j;
    }

    return lines.join('\n');
}

ValidationResult validate(const QString &unifiedDiff, int maxLines, int maxFiles)
{
    ValidationResult r;
    const QString normalizedDiff = normalize(unifiedDiff);
    if (normalizedDiff.trimmed().isEmpty())
    {
        r.error = QStringLiteral("Empty diff");
        return r;
    }

    const QStringList lines = normalizedDiff.split('\n');
    int changedLines = 0;
    QSet<QString> files;

    static const QRegularExpression diffHeader(R"(^--- [ab]/(.+)$)");
    static const QRegularExpression hunkHeader(R"(^@@ -\d+(?:,\d+)? \+\d+(?:,\d+)? @@)");

    bool inHunk = false;
    for (const QString &line : lines)
    {
        QRegularExpressionMatch m = diffHeader.match(line);
        if (m.hasMatch())
        {
            files.insert(m.captured(1));
            inHunk = false;
            continue;
        }
        if (hunkHeader.match(line).hasMatch())
        {
            inHunk = true;
            continue;
        }
        if (inHunk && (line.startsWith('+') || line.startsWith('-')))
        {
            // Skip the +++ / --- file header lines
            if (!line.startsWith("+++") && !line.startsWith("---"))
                ++changedLines;
        }
    }

    r.linesChanged = changedLines;
    r.filesChanged = static_cast<int>(files.size());

    if (r.filesChanged > maxFiles)
    {
        r.error = QStringLiteral("Too many files changed: %1 (max %2)")
                      .arg(r.filesChanged)
                      .arg(maxFiles);
        return r;
    }
    if (r.linesChanged > maxLines)
    {
        r.error = QStringLiteral("Too many lines changed: %1 (max %2)")
                      .arg(r.linesChanged)
                      .arg(maxLines);
        return r;
    }
    if (files.isEmpty())
    {
        r.error = QStringLiteral("No file headers found in diff");
        return r;
    }

    r.valid = true;
    return r;
}

static bool runPatchCmd(const QString &unifiedDiff, const QString &workDir,
                        const QStringList &extraArgs, QString &errorMsg)
{
    ProcessRunner runner;
    QStringList args = {"-p1"};
    args.append(extraArgs);

    auto result = runner.run(QStringLiteral("patch"), args, workDir, 15000, unifiedDiff);
    if (!result.success)
    {
        errorMsg = result.stdErr.isEmpty() ? result.errorString : result.stdErr;
        return false;
    }
    return true;
}

bool applyPatch(const QString &unifiedDiff, const QString &workDir, bool dryRun, QString &errorMsg)
{
    const QString normalizedDiff = normalize(unifiedDiff);
    QStringList extra;
    if (dryRun)
        extra << QStringLiteral("--dry-run");
    return runPatchCmd(normalizedDiff, workDir, extra, errorMsg);
}

bool revertPatch(const QString &unifiedDiff, const QString &workDir, QString &errorMsg)
{
    return runPatchCmd(normalize(unifiedDiff), workDir, {QStringLiteral("-R")}, errorMsg);
}

NewFileResult extractAndCreateNewFiles(const QString &patchText, const QString &workDir,
                                       bool dryRun)
{
    NewFileResult result;

    static const QRegularExpression newFileHeader(R"(^=== NEW FILE:\s*(.+?)\s*===$)");

    const QStringList lines = patchText.split('\n');
    QStringList diffLines;
    int i = 0;

    while (i < lines.size())
    {
        const auto m = newFileHeader.match(lines[i]);
        if (!m.hasMatch())
        {
            diffLines.append(lines[i]);
            ++i;
            continue;
        }

        const QString relPath = m.captured(1);
        ++i;

        // Collect file content until next section or end
        QStringList contentLines;
        while (i < lines.size())
        {
            if (newFileHeader.match(lines[i]).hasMatch())
                break;
            if (lines[i].startsWith(QStringLiteral("=== MODIFIED:")))
                break;
            contentLines.append(lines[i]);
            ++i;
        }

        // Trim trailing empty lines
        while (!contentLines.isEmpty() && contentLines.last().trimmed().isEmpty())
            contentLines.removeLast();

        const QString content = contentLines.join('\n') + QLatin1Char('\n');

        // Sandbox check
        const QDir dir(workDir);
        const QString absPath = dir.absoluteFilePath(relPath);
        if (!QFileInfo(absPath).absoluteFilePath().startsWith(dir.canonicalPath()))
        {
            result.error = QStringLiteral("New file path outside project: %1").arg(relPath);
            return result;
        }

        if (!dryRun)
        {
            // Create parent directories
            QFileInfo fi(absPath);
            if (!fi.dir().exists())
                QDir().mkpath(fi.absolutePath());

            QFile f(absPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                result.error =
                    QStringLiteral("Cannot create file %1: %2").arg(relPath, f.errorString());
                return result;
            }
            f.write(content.toUtf8());
        }

        result.createdFiles.append(relPath);
    }

    result.remainingDiff = diffLines.join('\n');
    return result;
}

}  // namespace qcai2::Diff
