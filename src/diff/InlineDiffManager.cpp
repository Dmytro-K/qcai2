/*! Implements parsing and inline presentation of unified diff hunks. */
#include "InlineDiffManager.h"
#include "../util/Diff.h"
#include "../util/Logger.h"

#include <coreplugin/editormanager/editormanager.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textmark.h>

#include <QAction>
#include <QDir>
#include <QRegularExpression>

namespace qcai2
{

/** Text mark category used for AI-generated diff hunks. */
static const TextEditor::TextMarkCategory kDiffCategory{QStringLiteral("AI Diff"),
                                                        Utils::Id("qcai2.DiffHunk")};

InlineDiffManager::InlineDiffManager(QObject *parent) : QObject(parent)
{
}

InlineDiffManager::~InlineDiffManager()
{
    clearAll();
}

// ── Diff parsing ───────────────────────────────────────────────

QList<DiffHunk> InlineDiffManager::parseDiff(const QString &unifiedDiff)
{
    QList<DiffHunk> hunks;
    const QStringList lines = unifiedDiff.split(QLatin1Char('\n'));

    static const QRegularExpression reFile(QStringLiteral(R"(^--- a/(.+)$)"));
    static const QRegularExpression reHunk(
        QStringLiteral(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)"));

    QString currentFile;
    QString fileHeaderA, fileHeaderB;

    for (int i = 0; i < lines.size(); ++i)
    {
        const QString &line = lines[i];

        // Track file headers
        auto mFile = reFile.match(line);
        if (mFile.hasMatch())
        {
            currentFile = mFile.captured(1);
            fileHeaderA = line;
            if (i + 1 < lines.size() && lines[i + 1].startsWith(QStringLiteral("+++ ")))
                fileHeaderB = lines[i + 1];
            continue;
        }

        if (line.startsWith(QStringLiteral("+++ ")))
            continue;

        // Parse hunk header
        auto mHunk = reHunk.match(line);
        if (!mHunk.hasMatch())
            continue;

        DiffHunk h;
        h.filePath = currentFile;
        h.startLineOld = mHunk.captured(1).toInt();
        h.countOld = mHunk.captured(2).isEmpty() ? 1 : mHunk.captured(2).toInt();
        h.startLineNew = mHunk.captured(3).toInt();
        h.countNew = mHunk.captured(4).isEmpty() ? 1 : mHunk.captured(4).toInt();
        h.hunkHeader = line;

        // Collect hunk body (until next hunk/file header or end)
        QStringList bodyLines;
        for (int j = i + 1; j < lines.size(); ++j)
        {
            const QString &bl = lines[j];
            if (bl.startsWith(QStringLiteral("--- ")) || bl.startsWith(QStringLiteral("diff ")) ||
                reHunk.match(bl).hasMatch())
                break;
            bodyLines.append(bl);
        }
        h.hunkBody = bodyLines.join(QLatin1Char('\n'));

        // Build a self-contained mini-patch for this hunk
        h.fullPatch = fileHeaderA + QLatin1Char('\n') + fileHeaderB + QLatin1Char('\n') +
                      h.hunkHeader + QLatin1Char('\n') + h.hunkBody + QLatin1Char('\n');

        hunks.append(std::move(h));
    }

    return hunks;
}

// ── Show diff ──────────────────────────────────────────────────

void InlineDiffManager::showDiff(const QString &unifiedDiff, const QString &projectDir)
{
    clearAll();
    m_projectDir = projectDir;

    auto parsed = parseDiff(unifiedDiff);
    QCAI_INFO("InlineDiff", QStringLiteral("Parsed %1 hunks from diff").arg(parsed.size()));

    for (auto &h : parsed)
    {
        m_hunks.append({std::move(h), nullptr});
    }

    // Create markers and open files
    QSet<QString> openedFiles;
    for (int i = 0; i < m_hunks.size(); ++i)
    {
        const QString absPath = QDir(m_projectDir).absoluteFilePath(m_hunks[i].hunk.filePath);

        // Open file in editor if not already opened
        if (!openedFiles.contains(absPath))
        {
            openedFiles.insert(absPath);
            Core::EditorManager::openEditorAt(
                Utils::Link(Utils::FilePath::fromString(absPath), 0, 0), {},
                Core::EditorManager::DoNotChangeCurrentEditor);
        }

        createMarker(i);
    }

    // Navigate to first hunk
    if (!m_hunks.isEmpty())
    {
        const auto &first = m_hunks[0].hunk;
        const QString absPath = QDir(m_projectDir).absoluteFilePath(first.filePath);
        Core::EditorManager::openEditorAt(
            Utils::Link(Utils::FilePath::fromString(absPath), first.startLineNew, 0));
    }
}

// ── Create marker ──────────────────────────────────────────────

void InlineDiffManager::createMarker(int index)
{
    const auto &h = m_hunks[index].hunk;
    const QString absPath = QDir(m_projectDir).absoluteFilePath(h.filePath);

    auto mark = new TextEditor::TextMark(Utils::FilePath::fromString(absPath), h.startLineNew,
                                         kDiffCategory);

    mark->setPriority(TextEditor::TextMark::HighPriority);
    mark->setColor(Utils::Theme::IconsInfoColor);
    mark->setVisible(true);

    // Summary annotation in the editor gutter
    int addedLines = 0, removedLines = 0;
    for (const auto &line : h.hunkBody.split(QLatin1Char('\n')))
    {
        if (line.startsWith(QLatin1Char('+')))
            ++addedLines;
        else if (line.startsWith(QLatin1Char('-')))
            ++removedLines;
    }
    mark->setLineAnnotation(QStringLiteral("AI: +%1/-%2").arg(addedLines).arg(removedLines));

    // Tooltip shows the full hunk diff
    mark->setToolTip(
        QStringLiteral("<pre>%1</pre>").arg(h.hunkHeader + QStringLiteral("\n") + h.hunkBody));

    // Accept/Reject actions in the gutter context menu
    mark->setActionsProvider([this, index]() -> QList<QAction *> {
        auto *acceptAction = new QAction(QStringLiteral("✅ Accept Hunk"));
        connect(acceptAction, &QAction::triggered, this, [this, index]() { acceptHunk(index); });

        auto *rejectAction = new QAction(QStringLiteral("❌ Reject Hunk"));
        connect(rejectAction, &QAction::triggered, this, [this, index]() { rejectHunk(index); });

        auto *acceptAllAction = new QAction(QStringLiteral("✅ Accept All"));
        connect(acceptAllAction, &QAction::triggered, this, [this]() { acceptAll(); });

        auto *rejectAllAction = new QAction(QStringLiteral("❌ Reject All"));
        connect(rejectAllAction, &QAction::triggered, this, [this]() { rejectAll(); });

        return {acceptAction, rejectAction, acceptAllAction, rejectAllAction};
    });

    m_hunks[index].mark = mark;
}

// ── Accept / Reject ────────────────────────────────────────────

void InlineDiffManager::acceptHunk(int hunkIndex)
{
    if (hunkIndex < 0 || hunkIndex >= m_hunks.size() || m_resolved.contains(hunkIndex))
        return;

    const auto &h = m_hunks[hunkIndex].hunk;
    const QString patchText = Diff::normalize(h.fullPatch);

    QString errorMsg;
    if (!Diff::applyPatch(patchText, m_projectDir, false, errorMsg))
    {
        QCAI_WARN("InlineDiff",
                  QStringLiteral("Failed to apply hunk %1: %2").arg(hunkIndex).arg(errorMsg));
        return;
    }

    QCAI_INFO("InlineDiff",
              QStringLiteral("Accepted hunk %1 in %2").arg(hunkIndex).arg(h.filePath));

    m_resolved.insert(hunkIndex);
    delete m_hunks[hunkIndex].mark;
    m_hunks[hunkIndex].mark = nullptr;
    emit hunkAccepted(hunkIndex, h.filePath);

    if (m_resolved.size() == m_hunks.size())
        emit allResolved();
}

void InlineDiffManager::rejectHunk(int hunkIndex)
{
    if (hunkIndex < 0 || hunkIndex >= m_hunks.size() || m_resolved.contains(hunkIndex))
        return;

    const auto &h = m_hunks[hunkIndex].hunk;
    QCAI_INFO("InlineDiff",
              QStringLiteral("Rejected hunk %1 in %2").arg(hunkIndex).arg(h.filePath));

    m_resolved.insert(hunkIndex);
    delete m_hunks[hunkIndex].mark;
    m_hunks[hunkIndex].mark = nullptr;
    emit hunkRejected(hunkIndex, h.filePath);

    if (m_resolved.size() == m_hunks.size())
        emit allResolved();
}

void InlineDiffManager::acceptAll()
{
    for (int i = 0; i < m_hunks.size(); ++i)
    {
        if (!m_resolved.contains(i))
            acceptHunk(i);
    }
}

void InlineDiffManager::rejectAll()
{
    for (int i = 0; i < m_hunks.size(); ++i)
    {
        if (!m_resolved.contains(i))
            rejectHunk(i);
    }
}

void InlineDiffManager::clearAll()
{
    for (auto &entry : m_hunks)
        delete entry.mark;
    m_hunks.clear();
    m_resolved.clear();
}

}  // namespace qcai2
