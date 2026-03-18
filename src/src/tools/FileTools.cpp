#include "FileTools.h"
#include "../util/Diff.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTextStream>

namespace qcai2
{

/**
 * Returns the JSON schema for read_file arguments.
 */
QJsonObject read_file_tool_t::args_schema() const
{
    return QJsonObject{{"path", QJsonObject{{"type", "string"}, {"required", true}}},
                       {"start_line", QJsonObject{{"type", "integer"}}},
                       {"end_line", QJsonObject{{"type", "integer"}}}};
}

/**
 * Reads a project file, optionally limited to a line range.
 * @param args JSON object containing path and optional line limits.
 * @param workDir Project root used for sandbox validation.
 * @return File contents, a numbered slice, or an error string.
 */
QString read_file_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    const QString rel_path = args.value("path").toString();
    if (rel_path.isEmpty() == true)
    {
        return QStringLiteral("Error: 'path' argument is required.");
    }

    const QString abs_path = QDir(work_dir).absoluteFilePath(rel_path);

    // Sandbox check: must stay within workDir
    if (((!QFileInfo(abs_path).canonicalFilePath().startsWith(QDir(work_dir).canonicalPath())) ==
         true))
    {
        return QStringLiteral("Error: path is outside the project directory.");
    }

    QFile file(abs_path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        return QStringLiteral("Error: cannot open file: %1").arg(file.errorString());
    }

    QTextStream in(&file);
    QString content = in.readAll();

    int start_line = args.value("start_line").toInt(0);
    int end_line = args.value("end_line").toInt(0);

    if (((start_line > 0 || end_line > 0) == true))
    {
        const QStringList lines = content.split('\n');
        if (((start_line < 1) == true))
        {
            start_line = 1;
        }
        if (end_line < 1 || end_line > lines.size())
        {
            end_line = static_cast<int>(lines.size());
        }
        QStringList slice;
        for (int i = start_line - 1; i < end_line && i < lines.size(); ++i)
        {
            slice.append(QStringLiteral("%1: %2").arg(i + 1).arg(lines[i]));
        }
        return slice.join('\n');
    }

    return content;
}

/**
 * Returns the JSON schema for apply_patch arguments.
 */
QJsonObject apply_patch_tool_t::args_schema() const
{
    return QJsonObject{{"diff", QJsonObject{{"type", "string"}, {"required", true}}}};
}

/**
 * Validates and applies a unified diff inside the project tree.
 * @param args JSON object containing the diff text.
 * @param workDir Project root used for file creation and patch application.
 * @return A success summary or an error string.
 */
QString apply_patch_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    const QString diff = args.value("diff").toString();
    if (diff.isEmpty() == true)
    {
        return QStringLiteral("Error: 'diff' argument is required.");
    }

    // Extract and create new files first
    auto nf = Diff::extract_and_create_new_files(diff, work_dir, /*dryRun=*/false);
    if (((!nf.error.isEmpty()) == true))
    {
        return QStringLiteral("Error creating new file: %1").arg(nf.error);
    }

    // Strip "=== MODIFIED: ..." header lines from the remaining diff
    const QStringList lines = nf.remaining_diff.split('\n');
    QStringList clean_lines;
    for (const QString &line : lines)
    {
        if (!line.startsWith(QStringLiteral("=== MODIFIED:")))
        {
            clean_lines.append(line);
        }
    }
    const QString unified_diff = clean_lines.join('\n').trimmed();

    int patch_lines = 0;
    int patch_files = 0;

    if (unified_diff.isEmpty() == false)
    {
        auto val = Diff::validate(unified_diff);
        if (((!val.valid) == true))
        {
            return QStringLiteral("Error: invalid diff: %1").arg(val.error);
        }

        patch_lines = val.lines_changed;
        patch_files = val.files_changed;

        // First do a dry run
        QString error_msg;
        if (Diff::apply_patch(unified_diff, work_dir, /*dryRun=*/true, error_msg) == false)
        {
            return QStringLiteral("Error: dry run failed: %1").arg(error_msg);
        }

        // Then apply for real
        if (Diff::apply_patch(unified_diff, work_dir, /*dryRun=*/false, error_msg) == false)
        {
            return QStringLiteral("Error: apply failed: %1").arg(error_msg);
        }
    }

    QStringList parts;
    if (((patch_lines > 0 || patch_files > 0) == true))
    {
        parts.append(QStringLiteral("%1 lines changed across %2 file(s)")
                         .arg(patch_lines)
                         .arg(patch_files));
    }
    if (((!nf.created_files.isEmpty()) == true))
    {
        parts.append(QStringLiteral("%1 new file(s) created: %2")
                         .arg(nf.created_files.size())
                         .arg(nf.created_files.join(QStringLiteral(", "))));
    }

    return QStringLiteral("Patch applied successfully. %1.").arg(parts.join(QStringLiteral("; ")));
}

}  // namespace qcai2
