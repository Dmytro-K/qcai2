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
QJsonObject ReadFileTool::argsSchema() const
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
QString ReadFileTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString relPath = args.value("path").toString();
    if (relPath.isEmpty())
        return QStringLiteral("Error: 'path' argument is required.");

    const QString absPath = QDir(workDir).absoluteFilePath(relPath);

    // Sandbox check: must stay within workDir
    if (!QFileInfo(absPath).canonicalFilePath().startsWith(QDir(workDir).canonicalPath()))
        return QStringLiteral("Error: path is outside the project directory.");

    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("Error: cannot open file: %1").arg(file.errorString());

    QTextStream in(&file);
    QString content = in.readAll();

    int startLine = args.value("start_line").toInt(0);
    int endLine = args.value("end_line").toInt(0);

    if (startLine > 0 || endLine > 0)
    {
        const QStringList lines = content.split('\n');
        if (startLine < 1)
            startLine = 1;
        if (endLine < 1 || endLine > lines.size())
            endLine = static_cast<int>(lines.size());
        QStringList slice;
        for (int i = startLine - 1; i < endLine && i < lines.size(); ++i)
            slice.append(QStringLiteral("%1: %2").arg(i + 1).arg(lines[i]));
        return slice.join('\n');
    }

    return content;
}

/**
 * Returns the JSON schema for apply_patch arguments.
 */
QJsonObject ApplyPatchTool::argsSchema() const
{
    return QJsonObject{{"diff", QJsonObject{{"type", "string"}, {"required", true}}}};
}

/**
 * Validates and applies a unified diff inside the project tree.
 * @param args JSON object containing the diff text.
 * @param workDir Project root used for file creation and patch application.
 * @return A success summary or an error string.
 */
QString ApplyPatchTool::execute(const QJsonObject &args, const QString &workDir)
{
    const QString diff = args.value("diff").toString();
    if (diff.isEmpty())
        return QStringLiteral("Error: 'diff' argument is required.");

    // Extract and create new files first
    auto nf = Diff::extractAndCreateNewFiles(diff, workDir, /*dryRun=*/false);
    if (!nf.error.isEmpty())
        return QStringLiteral("Error creating new file: %1").arg(nf.error);

    // Strip "=== MODIFIED: ..." header lines from the remaining diff
    const QStringList lines = nf.remainingDiff.split('\n');
    QStringList cleanLines;
    for (const QString &line : lines)
    {
        if (!line.startsWith(QStringLiteral("=== MODIFIED:")))
            cleanLines.append(line);
    }
    const QString unifiedDiff = cleanLines.join('\n').trimmed();

    int patchLines = 0;
    int patchFiles = 0;

    if (!unifiedDiff.isEmpty())
    {
        auto val = Diff::validate(unifiedDiff);
        if (!val.valid)
            return QStringLiteral("Error: invalid diff: %1").arg(val.error);

        patchLines = val.linesChanged;
        patchFiles = val.filesChanged;

        // First do a dry run
        QString errorMsg;
        if (!Diff::applyPatch(unifiedDiff, workDir, /*dryRun=*/true, errorMsg))
            return QStringLiteral("Error: dry run failed: %1").arg(errorMsg);

        // Then apply for real
        if (!Diff::applyPatch(unifiedDiff, workDir, /*dryRun=*/false, errorMsg))
            return QStringLiteral("Error: apply failed: %1").arg(errorMsg);
    }

    QStringList parts;
    if (patchLines > 0 || patchFiles > 0)
        parts.append(
            QStringLiteral("%1 lines changed across %2 file(s)").arg(patchLines).arg(patchFiles));
    if (!nf.createdFiles.isEmpty())
        parts.append(QStringLiteral("%1 new file(s) created: %2")
                         .arg(nf.createdFiles.size())
                         .arg(nf.createdFiles.join(QStringLiteral(", "))));

    return QStringLiteral("Patch applied successfully. %1.").arg(parts.join(QStringLiteral("; ")));
}

}  // namespace qcai2
