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
                                                        Utils::Id("qcai2.diff_hunk_t")};

namespace
{

QIcon standardIcon(QStyle::StandardPixmap icon)
{
    return qApp ? qApp->style()->standardIcon(icon) : QIcon();
}

QStringList preview_lines_for_hunk(const QString &hunk_body)
{
    constexpr int kMaxPreviewLines = 12;

    QStringList lines;
    int changedLineCount = 0;
    const QStringList bodyLines = hunk_body.split(QLatin1Char('\n'));
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

class diff_preview_widget_t final : public QFrame
{
public:
    diff_preview_widget_t(const QStringList &previewLines, const std::function<void()> &accept,
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

class diff_text_mark_t final : public TextEditor::TextMark
{
public:
    diff_text_mark_t(const Utils::FilePath &file_path, int lineNumber, const QString &toolTip,
                     std::function<void()> accept, std::function<void()> reject,
                     std::function<void()> accept_all, std::function<void()> reject_all)
        : TextEditor::TextMark(file_path, lineNumber, kDiffCategory), accept(std::move(accept)),
          reject(std::move(reject)), accept_all(std::move(accept_all)),
          reject_all(std::move(reject_all))
    {
        setPriority(TextEditor::TextMark::HighPriority);
        setColor(Utils::Theme::IconsInfoColor);
        setVisible(true);
        setIcon(standardIcon(QStyle::SP_DialogApplyButton));
        setToolTip(toolTip);
        setActionsProvider([accept = this->accept, reject = this->reject,
                            accept_all = this->accept_all, reject_all = this->reject_all] {
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
            QObject::connect(acceptAllAction, &QAction::triggered, acceptAllAction,
                             [accept_all]() {
                                 if (accept_all)
                                 {
                                     accept_all();
                                 }
                             });
            actions.append(acceptAllAction);

            auto *rejectAllAction = new QAction(standardIcon(QStyle::SP_DialogNoButton),
                                                QStringLiteral("Reject All Hunks"));
            rejectAllAction->setToolTip(QStringLiteral("Reject all remaining hunks"));
            QObject::connect(rejectAllAction, &QAction::triggered, rejectAllAction,
                             [reject_all]() {
                                 if (reject_all)
                                 {
                                     reject_all();
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
        if (this->accept)
        {
            this->accept();
        }
    }

private:
    std::function<void()> accept;
    std::function<void()> reject;
    std::function<void()> accept_all;
    std::function<void()> reject_all;
};

}  // namespace

inline_diff_manager_t::inline_diff_manager_t(QObject *parent) : QObject(parent)
{
}

inline_diff_manager_t::~inline_diff_manager_t()
{
    clear_all();
}

// ── Diff parsing ───────────────────────────────────────────────

QList<diff_hunk_t> inline_diff_manager_t::parse_diff(const QString &unifiedDiff)
{
    QList<diff_hunk_t> hunks;
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

        diff_hunk_t h;
        h.file_path = currentFile;
        h.start_line_old = mHunk.captured(1).toInt();
        h.count_old = mHunk.captured(2).isEmpty() ? 1 : mHunk.captured(2).toInt();
        h.start_line_new = mHunk.captured(3).toInt();
        h.count_new = mHunk.captured(4).isEmpty() ? 1 : mHunk.captured(4).toInt();
        h.hunk_header = line;

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
        h.hunk_body = bodyLines.join(QLatin1Char('\n'));

        // Build a self-contained mini-patch for this hunk
        h.full_patch = fileHeaderA + QLatin1Char('\n') + fileHeaderB + QLatin1Char('\n') +
                       h.hunk_header + QLatin1Char('\n') + h.hunk_body + QLatin1Char('\n');

        hunks.append(std::move(h));
    }

    return hunks;
}

// ── Show diff ──────────────────────────────────────────────────

void inline_diff_manager_t::show_diff(const QString &unifiedDiff, const QString &project_dir)
{
    this->clear_all();
    this->project_dir = project_dir;
    TextEditor::TextDocument::showMarksAnnotation(kDiffCategory.id);

    auto parsed = this->parse_diff(unifiedDiff);
    QCAI_INFO("InlineDiff", QStringLiteral("Parsed %1 hunks from diff").arg(parsed.size()));

    for (auto &h : parsed)
    {
        this->hunks.push_back({std::move(h), nullptr, {}});
    }

    // Create markers and open files
    QSet<QString> openedFiles;
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        const QString absPath =
            QDir(this->project_dir).absoluteFilePath(this->hunks[i].hunk.file_path);

        // Open file in editor if not already opened
        if (openedFiles.contains(absPath) == false)
        {
            openedFiles.insert(absPath);
            Core::EditorManager::openEditorAt(
                Utils::Link(Utils::FilePath::fromString(absPath), 0, 0), {},
                Core::EditorManager::DoNotChangeCurrentEditor);
        }

        this->create_marker(int(i));
    }

    // Navigate to first hunk
    if (!this->hunks.empty())
    {
        const auto &first = this->hunks[0].hunk;
        const QString absPath = QDir(this->project_dir).absoluteFilePath(first.file_path);
        Core::EditorManager::openEditorAt(
            Utils::Link(Utils::FilePath::fromString(absPath), first.start_line_new, 0));
    }
}

// ── Create marker ──────────────────────────────────────────────

void inline_diff_manager_t::create_marker(int index)
{
    const auto &h = this->hunks[std::size_t(index)].hunk;
    const QString absPath = QDir(this->project_dir).absoluteFilePath(h.file_path);

    const QString tooltip =
        QStringLiteral("<b>AI diff hunk</b><br/>Use the inline widget or gutter actions."
                       "<br/>Use the action buttons to reject or bulk-resolve."
                       "<pre>%1</pre>")
            .arg(h.hunk_header + QStringLiteral("\n") + h.hunk_body);
    const QStringList previewLines = preview_lines_for_hunk(h.hunk_body);

    auto *mark = new diff_text_mark_t(
        Utils::FilePath::fromString(absPath), h.start_line_new, tooltip,
        [this, index]() { accept_hunk(index); }, [this, index]() { this->reject_hunk(index); },
        [this]() { this->accept_all(); }, [this]() { this->reject_all(); });

    this->hunks[std::size_t(index)].mark = mark;

    const Utils::FilePath file_path = Utils::FilePath::fromString(absPath);
    const QList<TextEditor::BaseTextEditor *> editors =
        TextEditor::BaseTextEditor::textEditorsForFilePath(file_path);
    for (TextEditor::BaseTextEditor *editor : editors)
    {
        if (editor == nullptr || editor->editorWidget() == nullptr ||
            editor->textDocument() == nullptr || editor->textDocument()->document() == nullptr)
        {
            continue;
        }

        QTextBlock block =
            editor->textDocument()->document()->findBlockByNumber(h.start_line_new - 1);
        if (!block.isValid())
        {
            block = editor->textDocument()->document()->lastBlock();
        }
        if (!block.isValid())
        {
            continue;
        }

        auto *widget = new diff_preview_widget_t(
            previewLines, [this, index]() { this->accept_hunk(index); },
            [this, index]() { this->reject_hunk(index); }, editor->editorWidget());
        std::unique_ptr<TextEditor::EmbeddedWidgetInterface> handle =
            editor->editorWidget()->insertWidget(widget, block.position());
        if (handle)
        {
            this->hunks[std::size_t(index)].widget_handles.push_back(std::move(handle));
        }
        else
        {
            delete widget;
        }
    }
}

// ── Accept / Reject ────────────────────────────────────────────

void inline_diff_manager_t::accept_hunk(int hunkIndex)
{
    if (hunkIndex < 0 || hunkIndex >= int(this->hunks.size()) ||
        this->resolved.contains(hunkIndex))
    {
        return;
    }

    const auto &h = this->hunks[std::size_t(hunkIndex)].hunk;
    const QString patchText = Diff::normalize(h.full_patch);

    QString errorMsg;
    if (!Diff::apply_patch(patchText, this->project_dir, false, errorMsg))
    {
        QCAI_WARN("InlineDiff",
                  QStringLiteral("Failed to apply hunk %1: %2").arg(hunkIndex).arg(errorMsg));
        return;
    }

    QCAI_INFO("InlineDiff",
              QStringLiteral("Accepted hunk %1 in %2").arg(hunkIndex).arg(h.file_path));

    this->resolved.insert(hunkIndex);
    this->hunks[std::size_t(hunkIndex)].widget_handles.clear();
    delete this->hunks[std::size_t(hunkIndex)].mark;
    this->hunks[std::size_t(hunkIndex)].mark = nullptr;
    emit this->diff_changed(this->remaining_diff());
    emit this->hunk_accepted(hunkIndex, h.file_path);
    if (((this->resolved.size() == qsizetype(this->hunks.size())) == true))
    {
        emit this->all_resolved();
    }
}

void inline_diff_manager_t::reject_hunk(int hunkIndex)
{
    if (hunkIndex < 0 || hunkIndex >= int(this->hunks.size()) ||
        this->resolved.contains(hunkIndex))
    {
        return;
    }

    const auto &h = this->hunks[std::size_t(hunkIndex)].hunk;
    QCAI_INFO("InlineDiff",
              QStringLiteral("Rejected hunk %1 in %2").arg(hunkIndex).arg(h.file_path));

    this->resolved.insert(hunkIndex);
    this->hunks[std::size_t(hunkIndex)].widget_handles.clear();
    delete this->hunks[std::size_t(hunkIndex)].mark;
    this->hunks[std::size_t(hunkIndex)].mark = nullptr;
    emit this->diff_changed(this->remaining_diff());
    emit this->hunk_rejected(hunkIndex, h.file_path);
    if (((this->resolved.size() == qsizetype(this->hunks.size())) == true))
    {
        emit this->all_resolved();
    }
}

QString inline_diff_manager_t::remaining_diff() const
{
    QStringList patches;
    patches.reserve(qsizetype(this->hunks.size()));
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        if (this->resolved.contains(int(i)))
        {
            continue;
        }
        patches.append(Diff::normalize(this->hunks[i].hunk.full_patch).trimmed());
    }

    return patches.join(QStringLiteral("\n"));
}

void inline_diff_manager_t::accept_all()
{
    for (int i = 0; i < int(this->hunks.size()); ++i)
    {
        if (!this->resolved.contains(i))
        {
            this->accept_hunk(i);
        }
    }
}

void inline_diff_manager_t::reject_all()
{
    for (int i = 0; i < int(this->hunks.size()); ++i)
    {
        if (!this->resolved.contains(i))
        {
            this->reject_hunk(i);
        }
    }
}

void inline_diff_manager_t::clear_all()
{
    for (auto &entry : this->hunks)
    {
        entry.widget_handles.clear();
        delete entry.mark;
    }
    this->hunks.clear();
    this->resolved.clear();
}

}  // namespace qcai2
