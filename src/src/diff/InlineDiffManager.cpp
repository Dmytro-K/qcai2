/*! Implements parsing and inline presentation of unified diff hunks. */
#include "InlineDiffManager.h"
#include "../util/Diff.h"
#include "../util/Logger.h"

#include <coreplugin/editormanager/editormanager.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textmark.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTextBlock>
#include <QVBoxLayout>

#include <functional>

namespace qcai2
{

/** Text mark category used for AI-generated diff hunks. */
static const TextEditor::TextMarkCategory kDiffCategory{QStringLiteral("AI Diff"),
                                                        Utils::Id("qcai2.DiffHunk")};

namespace
{

QIcon standardIcon(QStyle::StandardPixmap icon)
{
    return qApp ? qApp->style()->standardIcon(icon) : QIcon();
}

QStringList previewLinesForHunk(const QString &hunkBody)
{
    constexpr int kMaxPreviewLines = 12;

    QStringList lines;
    int changedLineCount = 0;
    const QStringList bodyLines = hunkBody.split(QLatin1Char('\n'));
    for (const QString &line : bodyLines)
    {
        if ((!line.startsWith(QLatin1Char('+')) || line.startsWith(QStringLiteral("+++ "))) &&
            (!line.startsWith(QLatin1Char('-')) || line.startsWith(QStringLiteral("--- "))))
        {
            continue;
        }

        ++changedLineCount;
        lines.append(line);
        if (lines.size() >= kMaxPreviewLines)
        {
            break;
        }
    }

    if (((changedLineCount > kMaxPreviewLines) == true))
    {
        lines.append(QStringLiteral("…"));
    }

    return lines;
}

class DiffPreviewWidget final : public QFrame
{
public:
    DiffPreviewWidget(const QStringList &previewLines, const std::function<void()> &accept,
                      const std::function<void()> &reject, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("qcai2DiffPreviewWidget"));
        setFrameStyle(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(2, 2, 2, 2);
        root->setSpacing(2);

        const QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        for (const QString &line : previewLines)
        {
            auto *row = new QLabel(line.toHtmlEscaped(), this);
            row->setFont(codeFont);
            row->setTextInteractionFlags(Qt::TextSelectableByMouse);
            row->setWordWrap(false);
            row->setMargin(0);

            QString style =
                QStringLiteral("QLabel { border-radius: 3px; padding: 1px 5px; "
                               "background: palette(alternate-base); color: palette(text); }");
            if (line.startsWith(QLatin1Char('+')) && !line.startsWith(QStringLiteral("+++")))
            {
                style = QStringLiteral("QLabel { border-radius: 3px; padding: 1px 5px; "
                                       "background: rgba(70, 160, 95, 44); color: palette(text); "
                                       "border-left: 3px solid #4aa36a; }");
            }
            else if (line.startsWith(QLatin1Char('-')) && !line.startsWith(QStringLiteral("---")))
            {
                style = QStringLiteral("QLabel { border-radius: 3px; padding: 1px 5px; "
                                       "background: rgba(190, 80, 80, 44); color: palette(text); "
                                       "border-left: 3px solid #c06161; }");
            }

            row->setStyleSheet(style);
            root->addWidget(row);
        }

        auto *buttonsRow = new QHBoxLayout;
        buttonsRow->setContentsMargins(0, 0, 0, 0);
        buttonsRow->setSpacing(4);
        buttonsRow->addStretch(1);
        auto addCompactButton = [this, buttonsRow](const QString &text, const QString &style,
                                                   const std::function<void()> &callback) {
            auto *button = new QPushButton(text, this);
            button->setCursor(Qt::PointingHandCursor);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            button->setStyleSheet(style);
            QObject::connect(button, &QPushButton::clicked, this, [callback]() {
                if (callback)
                {
                    callback();
                }
            });
            buttonsRow->addWidget(button);
        };
        addCompactButton(
            QStringLiteral("Accept"),
            QStringLiteral("QPushButton { background: rgba(70, 160, 95, 56); "
                           "color: palette(text); border: 1px solid #4aa36a; "
                           "border-radius: 3px; padding: 0px 6px; min-height: 18px; }"),
            accept);
        addCompactButton(
            QStringLiteral("Reject"),
            QStringLiteral("QPushButton { background: rgba(190, 80, 80, 56); "
                           "color: palette(text); border: 1px solid #c06161; "
                           "border-radius: 3px; padding: 0px 6px; min-height: 18px; }"),
            reject);
        root->addLayout(buttonsRow);

        setStyleSheet(QStringLiteral(
            "#qcai2DiffPreviewWidget { background: transparent; border: 1px solid palette(mid); "
            "border-radius: 4px; }"));
    }
};

class DiffTextMark final : public TextEditor::TextMark
{
public:
    DiffTextMark(const Utils::FilePath &filePath, int lineNumber, const QString &toolTip,
                 std::function<void()> accept, std::function<void()> reject,
                 std::function<void()> acceptAll, std::function<void()> rejectAll)
        : TextEditor::TextMark(filePath, lineNumber, kDiffCategory), m_accept(std::move(accept)),
          m_reject(std::move(reject)), m_acceptAll(std::move(acceptAll)),
          m_rejectAll(std::move(rejectAll))
    {
        setPriority(TextEditor::TextMark::HighPriority);
        setColor(Utils::Theme::IconsInfoColor);
        setVisible(true);
        setIcon(standardIcon(QStyle::SP_DialogApplyButton));
        setToolTip(toolTip);
        setActionsProvider([accept = m_accept, reject = m_reject, acceptAll = m_acceptAll,
                            rejectAll = m_rejectAll] {
            QList<QAction *> actions;

            auto *acceptAction = new QAction(standardIcon(QStyle::SP_DialogApplyButton),
                                             QStringLiteral("Accept Hunk"));
            acceptAction->setToolTip(QStringLiteral("Accept this hunk"));
            QObject::connect(acceptAction, &QAction::triggered, acceptAction, [accept]() {
                if (accept)
                {
                    accept();
                }
            });
            actions.append(acceptAction);

            auto *rejectAction = new QAction(standardIcon(QStyle::SP_DialogCancelButton),
                                             QStringLiteral("Reject Hunk"));
            rejectAction->setToolTip(QStringLiteral("Reject this hunk"));
            QObject::connect(rejectAction, &QAction::triggered, rejectAction, [reject]() {
                if (reject)
                {
                    reject();
                }
            });
            actions.append(rejectAction);

            auto *acceptAllAction = new QAction(standardIcon(QStyle::SP_DialogYesButton),
                                                QStringLiteral("Accept All Hunks"));
            acceptAllAction->setToolTip(QStringLiteral("Accept all remaining hunks"));
            QObject::connect(acceptAllAction, &QAction::triggered, acceptAllAction, [acceptAll]() {
                if (acceptAll)
                {
                    acceptAll();
                }
            });
            actions.append(acceptAllAction);

            auto *rejectAllAction = new QAction(standardIcon(QStyle::SP_DialogNoButton),
                                                QStringLiteral("Reject All Hunks"));
            rejectAllAction->setToolTip(QStringLiteral("Reject all remaining hunks"));
            QObject::connect(rejectAllAction, &QAction::triggered, rejectAllAction, [rejectAll]() {
                if (rejectAll)
                {
                    rejectAll();
                }
            });
            actions.append(rejectAllAction);

            return actions;
        });
    }

    bool isClickable() const override
    {
        return true;
    }

    void clicked() override
    {
        if (m_accept)
        {
            m_accept();
        }
    }

private:
    std::function<void()> m_accept;
    std::function<void()> m_reject;
    std::function<void()> m_acceptAll;
    std::function<void()> m_rejectAll;
};

}  // namespace

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
            {
                fileHeaderB = lines[i + 1];
            }
            continue;
        }

        if (line.startsWith(QStringLiteral("+++ ")))
        {
            continue;
        }

        // Parse hunk header
        auto mHunk = reHunk.match(line);
        if (!mHunk.hasMatch())
        {
            continue;
        }

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
            {
                break;
            }
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
    TextEditor::TextDocument::showMarksAnnotation(kDiffCategory.id);

    auto parsed = parseDiff(unifiedDiff);
    QCAI_INFO("InlineDiff", QStringLiteral("Parsed %1 hunks from diff").arg(parsed.size()));

    for (auto &h : parsed)
    {
        m_hunks.push_back({std::move(h), nullptr, {}});
    }

    // Create markers and open files
    QSet<QString> openedFiles;
    for (std::size_t i = 0; i < m_hunks.size(); ++i)
    {
        const QString absPath = QDir(m_projectDir).absoluteFilePath(m_hunks[i].hunk.filePath);

        // Open file in editor if not already opened
        if (openedFiles.contains(absPath) == false)
        {
            openedFiles.insert(absPath);
            Core::EditorManager::openEditorAt(
                Utils::Link(Utils::FilePath::fromString(absPath), 0, 0), {},
                Core::EditorManager::DoNotChangeCurrentEditor);
        }

        createMarker(int(i));
    }

    // Navigate to first hunk
    if (!m_hunks.empty())
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
    const auto &h = m_hunks[std::size_t(index)].hunk;
    const QString absPath = QDir(m_projectDir).absoluteFilePath(h.filePath);

    const QString tooltip =
        QStringLiteral("<b>AI diff hunk</b><br/>Use the inline widget or gutter actions."
                       "<br/>Use the action buttons to reject or bulk-resolve."
                       "<pre>%1</pre>")
            .arg(h.hunkHeader + QStringLiteral("\n") + h.hunkBody);
    const QStringList previewLines = previewLinesForHunk(h.hunkBody);

    auto *mark = new DiffTextMark(
        Utils::FilePath::fromString(absPath), h.startLineNew, tooltip,
        [this, index]() { acceptHunk(index); }, [this, index]() { rejectHunk(index); },
        [this]() { acceptAll(); }, [this]() { rejectAll(); });

    m_hunks[std::size_t(index)].mark = mark;

    const Utils::FilePath filePath = Utils::FilePath::fromString(absPath);
    const QList<TextEditor::BaseTextEditor *> editors =
        TextEditor::BaseTextEditor::textEditorsForFilePath(filePath);
    for (TextEditor::BaseTextEditor *editor : editors)
    {
        if (editor == nullptr || editor->editorWidget() == nullptr ||
            editor->textDocument() == nullptr || editor->textDocument()->document() == nullptr)
        {
            continue;
        }

        QTextBlock block =
            editor->textDocument()->document()->findBlockByNumber(h.startLineNew - 1);
        if (!block.isValid())
        {
            block = editor->textDocument()->document()->lastBlock();
        }
        if (!block.isValid())
        {
            continue;
        }

        auto *widget = new DiffPreviewWidget(
            previewLines, [this, index]() { acceptHunk(index); },
            [this, index]() { rejectHunk(index); }, editor->editorWidget());
        std::unique_ptr<TextEditor::EmbeddedWidgetInterface> handle =
            editor->editorWidget()->insertWidget(widget, block.position());
        if (handle)
        {
            m_hunks[std::size_t(index)].widgetHandles.push_back(std::move(handle));
        }
        else
        {
            delete widget;
        }
    }
}

// ── Accept / Reject ────────────────────────────────────────────

void InlineDiffManager::acceptHunk(int hunkIndex)
{
    if (hunkIndex < 0 || hunkIndex >= int(m_hunks.size()) || m_resolved.contains(hunkIndex))
    {
        return;
    }

    const auto &h = m_hunks[std::size_t(hunkIndex)].hunk;
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
    m_hunks[std::size_t(hunkIndex)].widgetHandles.clear();
    delete m_hunks[std::size_t(hunkIndex)].mark;
    m_hunks[std::size_t(hunkIndex)].mark = nullptr;
    emit diffChanged(remainingDiff());
    emit hunkAccepted(hunkIndex, h.filePath);
    if (((m_resolved.size() == qsizetype(m_hunks.size())) == true))
    {
        emit allResolved();
    }
}

void InlineDiffManager::rejectHunk(int hunkIndex)
{
    if (hunkIndex < 0 || hunkIndex >= int(m_hunks.size()) || m_resolved.contains(hunkIndex))
    {
        return;
    }

    const auto &h = m_hunks[std::size_t(hunkIndex)].hunk;
    QCAI_INFO("InlineDiff",
              QStringLiteral("Rejected hunk %1 in %2").arg(hunkIndex).arg(h.filePath));

    m_resolved.insert(hunkIndex);
    m_hunks[std::size_t(hunkIndex)].widgetHandles.clear();
    delete m_hunks[std::size_t(hunkIndex)].mark;
    m_hunks[std::size_t(hunkIndex)].mark = nullptr;
    emit diffChanged(remainingDiff());
    emit hunkRejected(hunkIndex, h.filePath);
    if (((m_resolved.size() == qsizetype(m_hunks.size())) == true))
    {
        emit allResolved();
    }
}

QString InlineDiffManager::remainingDiff() const
{
    QStringList patches;
    patches.reserve(qsizetype(m_hunks.size()));
    for (std::size_t i = 0; i < m_hunks.size(); ++i)
    {
        if (m_resolved.contains(int(i)))
        {
            continue;
        }
        patches.append(Diff::normalize(m_hunks[i].hunk.fullPatch).trimmed());
    }

    return patches.join(QStringLiteral("\n"));
}

void InlineDiffManager::acceptAll()
{
    for (int i = 0; i < int(m_hunks.size()); ++i)
    {
        if (!m_resolved.contains(i))
        {
            acceptHunk(i);
        }
    }
}

void InlineDiffManager::rejectAll()
{
    for (int i = 0; i < int(m_hunks.size()); ++i)
    {
        if (!m_resolved.contains(i))
        {
            rejectHunk(i);
        }
    }
}

void InlineDiffManager::clearAll()
{
    for (auto &entry : m_hunks)
    {
        entry.widgetHandles.clear();
        delete entry.mark;
    }
    m_hunks.clear();
    m_resolved.clear();
}

}  // namespace qcai2
