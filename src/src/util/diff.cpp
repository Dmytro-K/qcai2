/*! @file
    @brief Implements unified diff normalization, validation, and patch helpers.
*/

#include "diff.h"
#include "logger.h"
#include "process_runner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace qcai2::Diff
{

namespace
{

QString truncate_patch_preview(const QString &text, int max_chars = 1200)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= max_chars)
    {
        return trimmed;
    }
    return trimmed.left(max_chars).trimmed() +
           QStringLiteral("\n... [truncated, %1 chars total]").arg(trimmed.size());
}

QString summarize_process_failure(const process_runner_t::result_t &result)
{
    QStringList parts;
    if (result.exit_code >= 0)
    {
        parts.append(QStringLiteral("exit code %1").arg(result.exit_code));
    }

    const QString std_err = result.std_err.trimmed();
    if (std_err.isEmpty() == false)
    {
        parts.append(QStringLiteral("stderr: %1").arg(std_err));
    }

    const QString std_out = result.std_out.trimmed();
    if (std_out.isEmpty() == false)
    {
        parts.append(QStringLiteral("stdout: %1").arg(std_out));
    }

    const QString process_error = result.error_string.trimmed();
    if (process_error.isEmpty() == false && process_error != QStringLiteral("Unknown error"))
    {
        parts.append(QStringLiteral("process: %1").arg(process_error));
    }

    if (parts.isEmpty() == true)
    {
        return QStringLiteral("patch failed without stderr/stdout; process state: %1")
            .arg(process_error.isEmpty() == false ? process_error : QStringLiteral("unknown"));
    }

    return parts.join(QStringLiteral("; "));
}

QString normalize_relative_project_path(const QString &path)
{
    const QString cleaned_path = QDir::cleanPath(path.trimmed());
    if (cleaned_path.isEmpty() == true || cleaned_path == QStringLiteral("."))
    {
        return {};
    }

    if (QDir::isAbsolutePath(cleaned_path) == true)
    {
        return {};
    }

#ifdef Q_OS_WIN
    static const QRegularExpression drive_prefixed_path(R"(^[A-Za-z]:)");
    if (drive_prefixed_path.match(cleaned_path).hasMatch() == true)
    {
        return {};
    }
#endif

    if (cleaned_path == QStringLiteral("..") ||
        cleaned_path.startsWith(QStringLiteral("../")) == true)
    {
        return {};
    }

    return cleaned_path;
}

#ifdef Q_OS_WIN
QString find_git_executable()
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty() == false)
    {
        return git;
    }

    const QStringList candidates = {
        QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("ProgramFiles"))) +
            QStringLiteral("/Git/cmd/git.exe"),
        QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("ProgramFiles"))) +
            QStringLiteral("/Git/bin/git.exe"),
        QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("ProgramFiles"))) +
            QStringLiteral("/Git/usr/bin/git.exe"),
        QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("ProgramW6432"))) +
            QStringLiteral("/Git/cmd/git.exe"),
        QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("ProgramW6432"))) +
            QStringLiteral("/Git/bin/git.exe"),
        QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("ProgramW6432"))) +
            QStringLiteral("/Git/usr/bin/git.exe"),
    };

    for (const QString &candidate : candidates)
    {
        if (candidate.isEmpty() == false && QFileInfo::exists(candidate) == true)
        {
            return candidate;
        }
    }

    return {};
}
#endif

}  // namespace

/**
 * @brief Runs the platform-specific diff-apply command with a normalized diff payload.
 * @param unifiedDiff Diff text passed on stdin to @c patch.
 * @param workDir Working directory used as the patch root.
 * @param dryRun When true, validates the diff without modifying files.
 * @param reverse When true, reverts a previously applied diff.
 * @param errorMsg Receives stderr or process error text on failure.
 * @return True when the patch command exits with code zero.
 */
static bool run_patch_cmd(const QString &unifiedDiff, const QString &workDir, bool dryRun,
                          bool reverse, QString &errorMsg);

QString normalize(const QString &unifiedDiff)
{
    const QStringList inputLines = unifiedDiff.split('\n');
    QStringList lines = inputLines;

    static const QRegularExpression hunkHeader(
        R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(.*)$)");

    for (int i = 0; i < lines.size();)
    {
        const auto m = hunkHeader.match(lines[i]);
        if (((!m.hasMatch()) == true))
        {
            ++i;
            continue;
        }

        int oldCount = 0;
        int newCount = 0;
        int j = i + 1;
        for (; ((j < lines.size()) == true); ++j)
        {
            const QString &l = lines[j];
            if (((l.startsWith("@@ -") || l.startsWith("--- ") || l.startsWith("diff --git ") ||
                  l.startsWith("Index: ")) == true))
            {
                break;
            }
            if (l.startsWith("\\ No newline at end of file") == true)
            {
                continue;
            }
            if (((l.startsWith('+') && !l.startsWith("+++ ")) == true))
            {
                ++newCount;
                continue;
            }
            if (((l.startsWith('-') && !l.startsWith("--- ")) == true))
            {
                ++oldCount;
                continue;
            }
            if (l.startsWith(' ') == true)
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

validation_result_t validate(const QString &unifiedDiff, int maxLines, int maxFiles)
{
    validation_result_t r;
    const QString normalizedDiff = normalize(unifiedDiff);
    if (((normalizedDiff.trimmed().isEmpty()) == true))
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
            {
                ++changedLines;
            }
        }
    }

    r.lines_changed = changedLines;
    r.files_changed = static_cast<int>(files.size());

    if (((r.files_changed > maxFiles) == true))
    {
        r.error = QStringLiteral("Too many files changed: %1 (max %2)")
                      .arg(r.files_changed)
                      .arg(maxFiles);
        return r;
    }
    if (((r.lines_changed > maxLines) == true))
    {
        r.error = QStringLiteral("Too many lines changed: %1 (max %2)")
                      .arg(r.lines_changed)
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

static bool run_patch_cmd(const QString &unifiedDiff, const QString &workDir, bool dryRun,
                          bool reverse, QString &errorMsg)
{
    const QString trimmed_work_dir = workDir.trimmed();
    if (trimmed_work_dir.isEmpty() == true)
    {
        errorMsg = QStringLiteral("Patch working directory is empty.");
        QCAI_WARN("Diff", errorMsg);
        return false;
    }

    if (QDir(trimmed_work_dir).exists() == false)
    {
        errorMsg =
            QStringLiteral("Patch working directory does not exist: %1").arg(trimmed_work_dir);
        QCAI_WARN("Diff", errorMsg);
        return false;
    }

    process_runner_t runner;
#ifdef Q_OS_WIN
    const QString program = find_git_executable();
    if (program.isEmpty() == true)
    {
        errorMsg = QStringLiteral("Git executable not found on PATH.");
        QCAI_WARN("Diff", errorMsg);
        return false;
    }
    QStringList args{QStringLiteral("-c"),    QStringLiteral("core.autocrlf=false"),
                     QStringLiteral("-c"),    QStringLiteral("core.safecrlf=false"),
                     QStringLiteral("apply"), QStringLiteral("--verbose"),
                     QStringLiteral("-p1")};
    if (dryRun == true)
    {
        args.append(QStringLiteral("--check"));
    }
    if (reverse == true)
    {
        args.append(QStringLiteral("-R"));
    }
#else
    const QString program = QStringLiteral("patch");
    QStringList args{QStringLiteral("-p1")};
    if (dryRun == true)
    {
        args.append(QStringLiteral("--dry-run"));
    }
    if (reverse == true)
    {
        args.append(QStringLiteral("-R"));
    }
#endif

    QCAI_INFO("Diff", QStringLiteral("Running %1 in %2 with args: %3")
                          .arg(program, trimmed_work_dir, args.join(QLatin1Char(' '))));
    QCAI_DEBUG("Diff",
               QStringLiteral("Patch preview:\n%1").arg(truncate_patch_preview(unifiedDiff)));

    auto result = runner.run(program, args, trimmed_work_dir, 15000, unifiedDiff);
    if (((!result.success) == true))
    {
        errorMsg = summarize_process_failure(result);
        QCAI_WARN("Diff",
                  QStringLiteral("%1 failed in %2 with args [%3]: %4")
                      .arg(program, trimmed_work_dir, args.join(QLatin1Char(' ')), errorMsg));
        QCAI_DEBUG(
            "Diff",
            QStringLiteral("Failed patch preview:\n%1").arg(truncate_patch_preview(unifiedDiff)));
        return false;
    }

    QCAI_INFO("Diff", QStringLiteral("%1 succeeded in %2").arg(program, trimmed_work_dir));
    return true;
}

bool apply_patch(const QString &unifiedDiff, const QString &workDir, bool dryRun,
                 QString &errorMsg)
{
    const QString normalizedDiff = normalize(unifiedDiff);
    return run_patch_cmd(normalizedDiff, workDir, dryRun, false, errorMsg);
}

bool revert_patch(const QString &unifiedDiff, const QString &workDir, QString &errorMsg)
{
    return run_patch_cmd(normalize(unifiedDiff), workDir, false, true, errorMsg);
}

new_file_result_t extract_and_create_new_files(const QString &patchText, const QString &workDir,
                                               bool dryRun)
{
    new_file_result_t result;

    static const QRegularExpression newFileHeader(R"(^=== NEW FILE:\s*(.+?)\s*===$)");

    const QStringList lines = patchText.split('\n');
    QStringList diffLines;
    int i = 0;

    while (i < lines.size())
    {
        const auto m = newFileHeader.match(lines[i]);
        if (((!m.hasMatch()) == true))
        {
            diffLines.append(lines[i]);
            ++i;
            continue;
        }

        const QString raw_rel_path = m.captured(1);
        const QString relPath = normalize_relative_project_path(raw_rel_path);
        if (relPath.isEmpty() == true)
        {
            result.error =
                QStringLiteral("New file path outside project: %1").arg(raw_rel_path.trimmed());
            return result;
        }
        ++i;

        // Collect file content until next section or end
        QStringList contentLines;
        while (i < lines.size())
        {
            if (newFileHeader.match(lines[i]).hasMatch())
            {
                break;
            }
            if (lines[i].startsWith(QStringLiteral("=== MODIFIED:")))
            {
                break;
            }
            contentLines.append(lines[i]);
            ++i;
        }

        // Trim trailing empty lines
        while (!contentLines.isEmpty() && contentLines.last().trimmed().isEmpty())
        {
            contentLines.removeLast();
        }

        const QString content = contentLines.join('\n') + QLatin1Char('\n');

        const QDir dir(workDir);
        const QString absPath = dir.filePath(relPath);

        if (dryRun == false)
        {
            // Create parent directories
            QFileInfo fi(absPath);
            if (((!fi.dir().exists()) == true))
            {
                QDir().mkpath(fi.absolutePath());
            }

            QFile f(absPath);
            if (f.open(QIODevice::WriteOnly) == false)
            {
                result.error =
                    QStringLiteral("Cannot create file %1: %2").arg(relPath, f.errorString());
                return result;
            }
            f.write(content.toUtf8());
        }

        result.created_files.append(relPath);
    }

    result.remaining_diff = diffLines.join('\n');
    return result;
}

}  // namespace qcai2::Diff
