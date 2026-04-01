/*! Implements parsing and inline presentation of unified diff hunks. */
#include "inline_diff_manager.h"
#include "../util/diff.h"
#include "../util/logger.h"
#include "diff_hunk_anchor.h"
#include "diff_hunk_text_edit.h"
#include "diff_preview_text_edit.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/idocument.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textmark.h>
#include <utils/changeset.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include <cstdio>
#include <functional>

namespace qcai2
{

/** Text mark category used for AI-generated diff hunks. */
static const TextEditor::TextMarkCategory kDiffCategory{QStringLiteral("AI Diff"),
                                                        Utils::Id("qcai2.diff_hunk_t")};

void flush_pending_layout_updates_for_file(const QString &project_dir, const QString &file_path);

namespace
{

QIcon standardIcon(QStyle::StandardPixmap icon)
{
    return qApp ? qApp->style()->standardIcon(icon) : QIcon();
}

bool inline_diff_trace_enabled()
{
    return qEnvironmentVariableIsSet("QCAI_INLINE_DIFF_TRACE");
}

Utils::Id diff_editor_id_for_file_path(const Utils::FilePath &file_path)
{
    Q_UNUSED(file_path);
    return Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
}

bool inline_diff_embedded_previews_enabled()
{
    return true;
}

bool inline_diff_line_annotations_enabled()
{
    return inline_diff_embedded_previews_enabled() == false;
}

QString pointer_string(const void *ptr)
{
    return QStringLiteral("0x%1").arg(quintptr(ptr), int(sizeof(quintptr) * 2), 16,
                                      QLatin1Char('0'));
}

QString document_path_string(const TextEditor::TextDocument *document)
{
    if (document == nullptr)
    {
        return QStringLiteral("<null>");
    }
    const QString file_path = document->filePath().toUrlishString();
    return file_path.isEmpty() == true ? QStringLiteral("<empty>") : file_path;
}

TextEditor::TextEditorWidget *editor_widget_for_editor(Core::IEditor *editor)
{
    if (editor == nullptr || editor->widget() == nullptr)
    {
        return nullptr;
    }

    if (auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(editor))
    {
        return text_editor->editorWidget();
    }

    if (auto *editor_widget = qobject_cast<TextEditor::TextEditorWidget *>(editor->widget()))
    {
        return editor_widget;
    }

    return editor->widget()->findChild<TextEditor::TextEditorWidget *>();
}

TextEditor::TextDocument *text_document_for_editor(Core::IEditor *editor)
{
    TextEditor::TextEditorWidget *editor_widget = editor_widget_for_editor(editor);
    return editor_widget != nullptr ? editor_widget->textDocument() : nullptr;
}

QList<Core::IEditor *> text_widget_editors_for_file_path(const Utils::FilePath &file_path)
{
    QList<Core::IEditor *> matching_editors;
    const QList<Core::IEditor *> editors = Core::DocumentModel::editorsForFilePath(file_path);
    for (Core::IEditor *editor : editors)
    {
        TextEditor::TextDocument *text_document = text_document_for_editor(editor);
        if (text_document == nullptr || text_document->filePath() != file_path)
        {
            continue;
        }

        matching_editors.append(editor);
    }

    return matching_editors;
}

QList<Core::IEditor *> text_widget_editors_for_document(TextEditor::TextDocument *document)
{
    QList<Core::IEditor *> matching_editors;
    if (document == nullptr)
    {
        return matching_editors;
    }

    const QList<Core::IEditor *> editors = Core::DocumentModel::editorsForDocument(document);
    for (Core::IEditor *editor : editors)
    {
        if (text_document_for_editor(editor) == document)
        {
            matching_editors.append(editor);
        }
    }

    return matching_editors;
}

QString document_path_string(const Core::IDocument *document)
{
    if (document == nullptr)
    {
        return QStringLiteral("<null>");
    }

    const QString file_path = document->filePath().toUrlishString();
    return file_path.isEmpty() == true ? QStringLiteral("<empty>") : file_path;
}

QString editor_debug_string(Core::IEditor *editor)
{
    if (editor == nullptr)
    {
        return QStringLiteral("editor=<null>");
    }

    const QString class_name = editor->metaObject() != nullptr
                                   ? QString::fromLatin1(editor->metaObject()->className())
                                   : QStringLiteral("<no-metaobject>");
    const QString document_path = document_path_string(editor->document());
    TextEditor::TextDocument *text_document = text_document_for_editor(editor);
    const QString text_document_path = text_document != nullptr
                                           ? document_path_string(text_document)
                                           : QStringLiteral("<not-text-widget>");
    return QStringLiteral("editor=%1 class=%2 document=%3 text_document=%4")
        .arg(pointer_string(editor))
        .arg(class_name, document_path, text_document_path);
}

void trace_inline_diff(const QString &message)
{
    if (inline_diff_trace_enabled() == false)
    {
        return;
    }

    const QString line =
        QStringLiteral("[%1] [InlineDiffTrace] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), message);
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    std::fflush(stderr);
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

QString preview_annotation_for_hunk(const QString &hunk_body)
{
    constexpr int kMaxAnnotationLines = 3;
    constexpr int kMaxAnnotationChars = 220;

    QStringList parts;
    const QStringList preview_lines = preview_lines_for_hunk(hunk_body);
    for (const QString &line : preview_lines)
    {
        if (parts.size() >= kMaxAnnotationLines)
        {
            break;
        }

        QString compact = line;
        if (compact.startsWith(QLatin1Char('+')) || compact.startsWith(QLatin1Char('-')))
        {
            compact = compact.left(1) + compact.mid(1).simplified();
        }
        else
        {
            compact = compact.simplified();
        }

        if (compact.isEmpty() == true)
        {
            continue;
        }

        parts.append(compact);
    }

    QString annotation = parts.join(QStringLiteral(" | "));
    if (preview_lines.size() > kMaxAnnotationLines)
    {
        if (annotation.isEmpty() == false)
        {
            annotation += QStringLiteral(" | ");
        }
        annotation += QStringLiteral("...");
    }
    if (annotation.size() > kMaxAnnotationChars)
    {
        annotation = annotation.left(kMaxAnnotationChars - 3).trimmed() + QStringLiteral("...");
    }

    return annotation;
}

int base_anchor_line_for_hunk(const diff_hunk_t &hunk)
{
    return first_change_old_line_for_hunk(hunk.start_line_old, hunk.start_line_new,
                                          hunk.hunk_body);
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

        auto *preview = new diff_preview_text_edit_t(previewLines, this);
        root->addWidget(preview);

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
                     const QString &annotation, std::function<void()> accept,
                     std::function<void()> reject, std::function<void()> accept_all,
                     std::function<void()> reject_all)
        : TextEditor::TextMark(file_path, lineNumber, kDiffCategory), accept(std::move(accept)),
          reject(std::move(reject)), accept_all(std::move(accept_all)),
          reject_all(std::move(reject_all))
    {
        setPriority(TextEditor::TextMark::HighPriority);
        setColor(Utils::Theme::IconsInfoColor);
        setVisible(true);
        setIcon(standardIcon(QStyle::SP_DialogApplyButton));
        setToolTip(toolTip);
        setLineAnnotation(annotation);
        setAnnotationTextFormat(Qt::PlainText);
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
    if (Core::EditorManager::instance() != nullptr)
    {
        connect(Core::EditorManager::instance(), &Core::EditorManager::editorCreated, this,
                [this](Core::IEditor *editor, const Utils::FilePath &file_path) {
                    trace_inline_diff(
                        QStringLiteral("editorCreated file=%1 %2")
                            .arg(file_path.toUrlishString(), editor_debug_string(editor)));
                    this->attach_widgets_for_editor(editor);
                });
        connect(Core::EditorManager::instance(), &Core::EditorManager::editorOpened, this,
                [this](Core::IEditor *editor) {
                    trace_inline_diff(
                        QStringLiteral("editorOpened %1").arg(editor_debug_string(editor)));
                    this->attach_widgets_for_editor(editor);
                });
        connect(
            Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged, this,
            [this](Core::IEditor *editor) {
                trace_inline_diff(
                    QStringLiteral("currentEditorChanged %1").arg(editor_debug_string(editor)));
                this->attach_widgets_for_editor(editor);
            });
    }
}

void inline_diff_manager_t::destroy_widget_handle(widget_handle_entry_t &entry)
{
    trace_inline_diff(
        QStringLiteral("destroy_widget_handle begin editor=%1 document=%2 widget=%3 carrier=%4 "
                       "handle=%5")
            .arg(pointer_string(entry.editor.data()), document_path_string(entry.document.data()),
                 pointer_string(entry.widget.data()),
                 pointer_string(entry.widget.isNull() ? nullptr : entry.widget->parentWidget()),
                 pointer_string(entry.handle.get())));
    if (entry.widget.isNull() == false)
    {
        if (entry.handle)
        {
            // Avoid Qt Creator's EmbeddedWidgetInterface::closed -> QTimer::singleShot(0,
            // layout->update()) during our explicit teardown path, because reload may process that
            // queued layout update while the document is being replaced.
            QObject::disconnect(entry.handle.get(), nullptr, nullptr, nullptr);
            entry.handle->blockSignals(true);
        }

        QWidget *widget = entry.widget.data();
        QPointer<QWidget> carrier = widget != nullptr ? widget->parentWidget() : nullptr;

        // Qt Creator removes the embedded-widget carrier from block user data in the
        // widget-destroyed callback, so the embedded widget must die before the carrier.
        delete widget;

        if (carrier.isNull() == false && carrier.data() != widget)
        {
            delete carrier.data();
        }
    }

    entry.widget = nullptr;
    entry.handle.reset();
    entry.editor = nullptr;
    entry.editor_widget = nullptr;
    entry.document = nullptr;
    trace_inline_diff(QStringLiteral("destroy_widget_handle end"));
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
    ++this->diff_generation;
    this->project_dir = project_dir;
    TextEditor::TextDocument::showMarksAnnotation(kDiffCategory.id);

    auto parsed = this->parse_diff(unifiedDiff);
    QCAI_INFO("InlineDiff", QStringLiteral("Parsed %1 hunks from diff").arg(parsed.size()));
    trace_inline_diff(QStringLiteral("show_diff project_dir=%1 parsed_hunks=%2")
                          .arg(project_dir)
                          .arg(parsed.size()));

    QSet<QString> hunk_file_paths;
    for (auto &h : parsed)
    {
        hunk_file_paths.insert(h.file_path);
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
            const Utils::FilePath file_path = Utils::FilePath::fromString(absPath);
            bool new_editor = false;
            Core::IEditor *editor = Core::EditorManager::openEditorAt(
                Utils::Link(file_path, 0, 0), diff_editor_id_for_file_path(file_path),
                Core::EditorManager::DoNotChangeCurrentEditor, &new_editor);
            trace_inline_diff(QStringLiteral("show_diff openEditorAt file=%1 new_editor=%2 %3")
                                  .arg(file_path.toUrlishString())
                                  .arg(new_editor)
                                  .arg(editor_debug_string(editor)));
            this->attach_widgets_for_editor(editor);
        }

        this->create_marker(int(i));
    }

    // Navigate to first hunk
    if (!this->hunks.empty())
    {
        const auto &first = this->hunks[0].hunk;
        const QString absPath = QDir(this->project_dir).absoluteFilePath(first.file_path);
        const Utils::FilePath file_path = Utils::FilePath::fromString(absPath);
        bool new_editor = false;
        Core::IEditor *editor = Core::EditorManager::openEditorAt(
            Utils::Link(file_path, this->current_document_anchor_line(0), 0),
            diff_editor_id_for_file_path(file_path), {}, &new_editor);
        trace_inline_diff(
            QStringLiteral("show_diff navigateEditorAt file=%1 line=%2 new_editor=%3 %4")
                .arg(file_path.toUrlishString())
                .arg(this->current_document_anchor_line(0))
                .arg(new_editor)
                .arg(editor_debug_string(editor)));
        this->attach_widgets_for_editor(editor);
    }

    // Some editor instances become discoverable only after the open/navigation work above fully
    // settles. Retry widget attachment once in the next event-loop turn so inline previews do not
    // disappear behind gutter-only marks when the editor materializes slightly later.
    if (inline_diff_embedded_previews_enabled() == true)
    {
        const int generation = this->diff_generation;
        QTimer::singleShot(0, this, [this, generation, hunk_file_paths]() {
            if (generation != this->diff_generation)
            {
                return;
            }

            for (const QString &file_path : hunk_file_paths)
            {
                this->reattach_widgets_for_file(file_path);
            }
        });
    }
}

// ── Create marker ──────────────────────────────────────────────

void inline_diff_manager_t::attach_hunk_widget(int index, Core::IEditor *editor)
{
    if (inline_diff_embedded_previews_enabled() == false)
    {
        Q_UNUSED(index);
        Q_UNUSED(editor);
        return;
    }

    auto &entry = this->hunks[std::size_t(index)];
    const auto &h = entry.hunk;
    TextEditor::TextEditorWidget *editor_widget = editor_widget_for_editor(editor);
    TextEditor::TextDocument *text_document = text_document_for_editor(editor);
    if (entry.mark == nullptr || editor == nullptr || editor_widget == nullptr ||
        text_document == nullptr || text_document->document() == nullptr)
    {
        trace_inline_diff(
            QStringLiteral("attach_hunk_widget skipped missing-editor-parts index=%1 file=%2 %3")
                .arg(index)
                .arg(h.file_path, editor_debug_string(editor)));
        return;
    }

    this->track_document(text_document);

    for (auto it = entry.widget_handles.begin(); it != entry.widget_handles.end();)
    {
        if (it->editor.isNull() == true || it->editor_widget.isNull() == true ||
            it->document.isNull() == true)
        {
            it = entry.widget_handles.erase(it);
            continue;
        }
        if (it->editor == editor || it->editor_widget == editor_widget)
        {
            return;
        }
        ++it;
    }

    const int anchor_line = this->current_document_anchor_line(index);
    QTextBlock block = text_document->document()->findBlockByNumber(anchor_line - 1);
    if (!block.isValid())
    {
        block = text_document->document()->lastBlock();
    }
    if (!block.isValid())
    {
        trace_inline_diff(
            QStringLiteral("attach_hunk_widget skipped invalid block index=%1 file=%2 editor=%3 "
                           "document=%4 anchor_line=%5")
                .arg(index)
                .arg(h.file_path)
                .arg(pointer_string(editor))
                .arg(document_path_string(text_document))
                .arg(anchor_line));
        return;
    }

    trace_inline_diff(QStringLiteral("attach_hunk_widget begin index=%1 file=%2 editor=%3 "
                                     "document=%4 anchor_line=%5 block=%6")
                          .arg(index)
                          .arg(h.file_path)
                          .arg(pointer_string(editor))
                          .arg(document_path_string(text_document))
                          .arg(anchor_line)
                          .arg(block.blockNumber() + 1));

    const QStringList preview_lines = preview_lines_for_hunk(h.hunk_body);
    auto *widget = new diff_preview_widget_t(
        preview_lines, [this, index]() { this->accept_hunk(index); },
        [this, index]() { this->reject_hunk(index); }, editor_widget);
    std::unique_ptr<TextEditor::EmbeddedWidgetInterface> handle =
        editor_widget->insertWidget(widget, block.position());
    if (handle)
    {
        QObject::connect(
            widget, &QObject::destroyed, this,
            [index, file_path = h.file_path, widget_ptr = QPointer<QObject>(widget)]() {
                trace_inline_diff(
                    QStringLiteral("preview widget destroyed index=%1 file=%2 widget=%3")
                        .arg(index)
                        .arg(file_path)
                        .arg(pointer_string(widget_ptr.data())));
            });
        QObject::connect(handle.get(), &QObject::destroyed, this,
                         [index, file_path = h.file_path, handle_ptr = handle.get()]() {
                             trace_inline_diff(
                                 QStringLiteral("embedded handle destroyed index=%1 file=%2 "
                                                "handle=%3")
                                     .arg(index)
                                     .arg(file_path)
                                     .arg(pointer_string(handle_ptr)));
                         });
        entry.widget_handles.push_back(
            {editor, editor_widget, text_document, widget, std::move(handle)});
        trace_inline_diff(QStringLiteral("attach_hunk_widget inserted index=%1 file=%2 editor=%3 "
                                         "document=%4 widget=%5 carrier=%6 handle=%7")
                              .arg(index)
                              .arg(h.file_path)
                              .arg(pointer_string(editor))
                              .arg(document_path_string(text_document))
                              .arg(pointer_string(widget))
                              .arg(pointer_string(widget->parentWidget()))
                              .arg(pointer_string(entry.widget_handles.back().handle.get())));
    }
    else
    {
        trace_inline_diff(
            QStringLiteral("attach_hunk_widget insert failed index=%1 file=%2 editor=%3 "
                           "document=%4 widget=%5")
                .arg(index)
                .arg(h.file_path)
                .arg(pointer_string(editor))
                .arg(document_path_string(text_document))
                .arg(pointer_string(widget)));
        delete widget;
    }
}

void inline_diff_manager_t::attach_widgets_for_editor(Core::IEditor *editor)
{
    trace_inline_diff(
        QStringLiteral("attach_widgets_for_editor called %1").arg(editor_debug_string(editor)));

    TextEditor::TextDocument *text_document = text_document_for_editor(editor);
    if (text_document == nullptr)
    {
        trace_inline_diff(
            QStringLiteral("attach_widgets_for_editor skipped missing-text-widget %1")
                .arg(editor_debug_string(editor)));
        return;
    }

    const Utils::FilePath editor_path = text_document->filePath();
    if (editor_path.isEmpty())
    {
        trace_inline_diff(QStringLiteral("attach_widgets_for_editor skipped empty-path %1")
                              .arg(editor_debug_string(editor)));
        return;
    }

    bool has_matching_hunk = false;
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        if (this->resolved.contains(int(i)) == true)
        {
            continue;
        }

        const Utils::FilePath hunk_path = Utils::FilePath::fromString(
            QDir(this->project_dir).absoluteFilePath(this->hunks[i].hunk.file_path));
        if (hunk_path == editor_path)
        {
            has_matching_hunk = true;
            if (inline_diff_embedded_previews_enabled() == false)
            {
                break;
            }
            this->attach_hunk_widget(int(i), editor);
        }
    }

    if (has_matching_hunk == true)
    {
        this->track_document(text_document);
        trace_inline_diff(QStringLiteral("attach_widgets_for_editor matched file=%1 %2")
                              .arg(editor_path.toUrlishString(), editor_debug_string(editor)));
    }
    else
    {
        trace_inline_diff(QStringLiteral("attach_widgets_for_editor no-match file=%1 %2")
                              .arg(editor_path.toUrlishString(), editor_debug_string(editor)));
    }
}

bool inline_diff_manager_t::has_unresolved_hunks_for_file(const QString &file_path) const
{
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        if (this->resolved.contains(int(i)) == true)
        {
            continue;
        }

        if (this->hunks[i].hunk.file_path == file_path)
        {
            return true;
        }
    }

    return false;
}

void inline_diff_manager_t::suspend_annotations_for_reload(const QString &file_path)
{
    if (file_path.isEmpty() == true || this->reload_annotation_hidden_files.contains(file_path))
    {
        return;
    }

    this->reload_annotation_hidden_files.insert(file_path);
    if (this->reload_annotation_hidden_files.size() != 1)
    {
        return;
    }

    if (TextEditor::TextDocument::marksAnnotationHidden(kDiffCategory.id) == true)
    {
        this->annotations_hidden_by_reload = false;
        trace_inline_diff(
            QStringLiteral("suspend_annotations_for_reload skipped already hidden file=%1")
                .arg(file_path));
        return;
    }

    this->annotations_hidden_by_reload = true;
    trace_inline_diff(QStringLiteral("suspend_annotations_for_reload file=%1").arg(file_path));
    TextEditor::TextDocument::temporaryHideMarksAnnotation(kDiffCategory.id);
}

void inline_diff_manager_t::resume_annotations_after_reload(const QString &file_path)
{
    if (file_path.isEmpty() == true)
    {
        return;
    }

    this->reload_annotation_hidden_files.remove(file_path);
    if (this->reload_annotation_hidden_files.isEmpty() == false ||
        this->annotations_hidden_by_reload == false)
    {
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (this->reload_annotation_hidden_files.isEmpty() == false ||
            this->annotations_hidden_by_reload == false)
        {
            return;
        }

        trace_inline_diff(QStringLiteral("resume_annotations_after_reload"));
        TextEditor::TextDocument::showMarksAnnotation(kDiffCategory.id);
        this->annotations_hidden_by_reload = false;
    });
}

void inline_diff_manager_t::track_document(TextEditor::TextDocument *document)
{
    if (document == nullptr)
    {
        return;
    }

    for (auto it = this->tracked_documents.begin(); it != this->tracked_documents.end();)
    {
        if (it->isNull() == true)
        {
            it = this->tracked_documents.erase(it);
            continue;
        }
        if (it->data() == document)
        {
            return;
        }
        ++it;
    }

    this->tracked_documents.push_back(document);
    trace_inline_diff(QStringLiteral("track_document document=%1 path=%2 tracked_count=%3")
                          .arg(pointer_string(document), document_path_string(document))
                          .arg(this->tracked_documents.size()));

    connect(document, &TextEditor::TextDocument::aboutToReload, this, [this, document]() {
        trace_inline_diff(QStringLiteral("document aboutToReload document=%1 path=%2")
                              .arg(pointer_string(document), document_path_string(document)));
        this->detach_widgets_for_document(document);

        const QString file_path =
            QDir(this->project_dir).relativeFilePath(document->filePath().toUrlishString());
        if (this->has_unresolved_hunks_for_file(file_path) == true)
        {
            this->suspend_annotations_for_reload(file_path);
        }
        if (file_path.isEmpty() == false &&
            this->pending_mark_reattach_files.contains(file_path) == false)
        {
            this->pending_external_reload_invalidation_files.insert(file_path);
        }
    });
    connect(
        document, &TextEditor::TextDocument::reloadFinished, this, [this, document](bool success) {
            const QString file_path =
                QDir(this->project_dir).relativeFilePath(document->filePath().toUrlishString());
            trace_inline_diff(QStringLiteral("document reloadFinished success=%1 document=%2 "
                                             "path=%3")
                                  .arg(success ? QStringLiteral("true") : QStringLiteral("false"),
                                       pointer_string(document), document_path_string(document)));
            if (success == true)
            {
                const Utils::FilePath document_path = document->filePath();
                QString external_invalidation_file_path;
                for (auto it = this->pending_mark_reattach_files.begin();
                     it != this->pending_mark_reattach_files.end();)
                {
                    const Utils::FilePath pending_path =
                        Utils::FilePath::fromString(QDir(this->project_dir).absoluteFilePath(*it));
                    if (pending_path == document_path)
                    {
                        const QString file_path = *it;
                        it = this->pending_mark_reattach_files.erase(it);
                        this->reattach_marks_for_file(file_path);
                        break;
                    }
                    ++it;
                }

                for (auto it = this->pending_external_reload_invalidation_files.begin();
                     it != this->pending_external_reload_invalidation_files.end();)
                {
                    const Utils::FilePath pending_path =
                        Utils::FilePath::fromString(QDir(this->project_dir).absoluteFilePath(*it));
                    if (pending_path == document_path)
                    {
                        external_invalidation_file_path = *it;
                        it = this->pending_external_reload_invalidation_files.erase(it);
                        break;
                    }
                    ++it;
                }

                if (external_invalidation_file_path.isEmpty() == false)
                {
                    const int generation = this->diff_generation;
                    QTimer::singleShot(
                        0, this, [this, external_invalidation_file_path, generation]() {
                            if (generation != this->diff_generation)
                            {
                                return;
                            }
                            this->invalidate_hunks_for_file(external_invalidation_file_path);
                        });
                }
            }
            this->reattach_widgets_for_document(document);
            this->resume_annotations_after_reload(file_path);
        });
    connect(
        document, &TextEditor::TextDocument::contentsChangedWithPosition, this,
        [this, document](int position, int chars_removed, int chars_added) {
            Q_UNUSED(position);
            Q_UNUSED(chars_removed);
            Q_UNUSED(chars_added);

            const QString file_path =
                QDir(this->project_dir).relativeFilePath(document->filePath().toUrlishString());
            if (file_path.isEmpty() == true)
            {
                return;
            }

            if (this->internal_document_change_files.contains(file_path) == true)
            {
                trace_inline_diff(
                    QStringLiteral("document contentsChanged ignored internal file=%1")
                        .arg(file_path));
                return;
            }

            if (this->has_unresolved_hunks_for_file(file_path) == false ||
                this->pending_local_edit_invalidation_files.contains(file_path) == true)
            {
                return;
            }

            trace_inline_diff(
                QStringLiteral("document contentsChanged scheduling local invalidation file=%1")
                    .arg(file_path));
            this->pending_local_edit_invalidation_files.insert(file_path);
            const int generation = this->diff_generation;
            QTimer::singleShot(0, this, [this, file_path, generation]() {
                this->pending_local_edit_invalidation_files.remove(file_path);
                if (generation != this->diff_generation)
                {
                    return;
                }
                if (this->internal_document_change_files.contains(file_path) == true ||
                    this->has_unresolved_hunks_for_file(file_path) == false)
                {
                    return;
                }
                this->invalidate_hunks_for_file(file_path);
            });
        });
    connect(document, &QObject::destroyed, this, [this]() {
        trace_inline_diff(QStringLiteral("tracked document destroyed"));
        for (auto it = this->tracked_documents.begin(); it != this->tracked_documents.end();)
        {
            if (it->isNull() == true)
            {
                it = this->tracked_documents.erase(it);
                continue;
            }
            ++it;
        }
    });
}

void inline_diff_manager_t::detach_widgets_for_document(TextEditor::TextDocument *document)
{
    if (document == nullptr)
    {
        return;
    }

    trace_inline_diff(QStringLiteral("detach_widgets_for_document begin document=%1 path=%2")
                          .arg(pointer_string(document), document_path_string(document)));

    for (auto &entry : this->hunks)
    {
        for (auto it = entry.widget_handles.begin(); it != entry.widget_handles.end();)
        {
            if (it->editor.isNull() == true || it->document.isNull() == true)
            {
                it = entry.widget_handles.erase(it);
                continue;
            }

            if (it->document != document)
            {
                ++it;
                continue;
            }

            trace_inline_diff(
                QStringLiteral("detach_widgets_for_document remove editor=%1 "
                               "document=%2 widget=%3 carrier=%4 handle=%5")
                    .arg(
                        pointer_string(it->editor.data()),
                        document_path_string(it->document.data()),
                        pointer_string(it->widget.data()),
                        pointer_string(it->widget.isNull() ? nullptr : it->widget->parentWidget()),
                        pointer_string(it->handle.get())));
            destroy_widget_handle(*it);
            it = entry.widget_handles.erase(it);
        }
    }

    trace_inline_diff(QStringLiteral("detach_widgets_for_document end"));
}

void inline_diff_manager_t::reattach_widgets_for_document(TextEditor::TextDocument *document)
{
    if (document == nullptr)
    {
        return;
    }

    const Utils::FilePath document_path = document->filePath();
    if (document_path.isEmpty() == true)
    {
        return;
    }

    const QList<Core::IEditor *> editors = text_widget_editors_for_document(document);
    trace_inline_diff(
        QStringLiteral("reattach_widgets_for_document document=%1 path=%2 editors=%3")
            .arg(pointer_string(document), document_path_string(document))
            .arg(editors.size()));
    if (editors.isEmpty() == true)
    {
        return;
    }

    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        if (this->resolved.contains(int(i)) == true)
        {
            continue;
        }

        const Utils::FilePath hunk_path = Utils::FilePath::fromString(
            QDir(this->project_dir).absoluteFilePath(this->hunks[i].hunk.file_path));
        if (hunk_path != document_path)
        {
            continue;
        }

        for (Core::IEditor *editor : editors)
        {
            this->attach_hunk_widget(int(i), editor);
        }
    }
}

void inline_diff_manager_t::detach_widgets_for_file(const QString &file_path)
{
    trace_inline_diff(QStringLiteral("detach_widgets_for_file begin file=%1").arg(file_path));

    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        auto &entry = this->hunks[i];
        if (this->resolved.contains(int(i)) == true || entry.hunk.file_path != file_path)
        {
            continue;
        }

        for (auto &handle_entry : entry.widget_handles)
        {
            destroy_widget_handle(handle_entry);
        }
        entry.widget_handles.clear();
    }

    trace_inline_diff(QStringLiteral("detach_widgets_for_file end"));
}

void inline_diff_manager_t::reattach_widgets_for_file(const QString &file_path)
{
    const QString abs_path = QDir(this->project_dir).absoluteFilePath(file_path);
    const QList<Core::IEditor *> editors =
        text_widget_editors_for_file_path(Utils::FilePath::fromString(abs_path));

    trace_inline_diff(QStringLiteral("reattach_widgets_for_file file=%1 editors=%2")
                          .arg(file_path)
                          .arg(editors.size()));

    for (Core::IEditor *editor : editors)
    {
        trace_inline_diff(QStringLiteral("reattach_widgets_for_file editor file=%1 %2")
                              .arg(file_path, editor_debug_string(editor)));
    }

    if (editors.isEmpty() == true)
    {
        return;
    }

    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        if (this->resolved.contains(int(i)) == true || this->hunks[i].hunk.file_path != file_path)
        {
            continue;
        }

        for (Core::IEditor *editor : editors)
        {
            this->attach_hunk_widget(int(i), editor);
        }
    }
}

void inline_diff_manager_t::detach_marks_for_file(const QString &file_path)
{
    trace_inline_diff(QStringLiteral("detach_marks_for_file begin file=%1").arg(file_path));

    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        auto &entry = this->hunks[i];
        if (this->resolved.contains(int(i)) == true || entry.hunk.file_path != file_path ||
            entry.mark == nullptr)
        {
            continue;
        }

        delete entry.mark;
        entry.mark = nullptr;
    }

    trace_inline_diff(QStringLiteral("detach_marks_for_file end"));
}

void inline_diff_manager_t::reattach_marks_for_file(const QString &file_path)
{
    trace_inline_diff(QStringLiteral("reattach_marks_for_file begin file=%1").arg(file_path));

    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        auto &entry = this->hunks[i];
        if (this->resolved.contains(int(i)) == true || entry.hunk.file_path != file_path ||
            entry.mark != nullptr)
        {
            continue;
        }

        this->create_marker(int(i));
    }

    trace_inline_diff(QStringLiteral("reattach_marks_for_file end"));
}

void inline_diff_manager_t::invalidate_hunks_for_file(const QString &file_path)
{
    trace_inline_diff(QStringLiteral("invalidate_hunks_for_file begin file=%1").arg(file_path));

    QList<int> invalidated_indexes;
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        auto &entry = this->hunks[i];
        if (this->resolved.contains(int(i)) == true || entry.hunk.file_path != file_path)
        {
            continue;
        }

        for (auto &widget_handle : entry.widget_handles)
        {
            destroy_widget_handle(widget_handle);
        }
        entry.widget_handles.clear();

        delete entry.mark;
        entry.mark = nullptr;

        this->resolved.insert(int(i));
        this->rejected.insert(int(i));
        invalidated_indexes.append(int(i));
    }

    if (invalidated_indexes.isEmpty() == true)
    {
        trace_inline_diff(QStringLiteral("invalidate_hunks_for_file end invalidated=0"));
        return;
    }

    this->pending_mark_reattach_files.remove(file_path);
    this->pending_local_edit_invalidation_files.remove(file_path);
    QCAI_WARN("InlineDiff",
              QStringLiteral("Invalidated %1 unresolved inline diff hunk(s) for %2 after "
                             "document contents changed")
                  .arg(invalidated_indexes.size())
                  .arg(file_path));

    emit this->diff_changed(this->remaining_diff());
    for (int index : std::as_const(invalidated_indexes))
    {
        emit this->hunk_rejected(index, file_path);
    }

    if (this->resolved.size() == qsizetype(this->hunks.size()))
    {
        emit this->all_resolved();
    }

    trace_inline_diff(QStringLiteral("invalidate_hunks_for_file end invalidated=%1")
                          .arg(invalidated_indexes.size()));
}

void flush_pending_layout_updates_for_file(const QString &project_dir, const QString &file_path)
{
    const QString abs_path = QDir(project_dir).absoluteFilePath(file_path);
    const QList<Core::IEditor *> editors =
        text_widget_editors_for_file_path(Utils::FilePath::fromString(abs_path));

    trace_inline_diff(QStringLiteral("flush_pending_layout_updates_for_file file=%1 editors=%2")
                          .arg(file_path)
                          .arg(editors.size()));

    for (Core::IEditor *editor : editors)
    {
        TextEditor::TextDocument *text_document = text_document_for_editor(editor);
        if (editor == nullptr || text_document == nullptr || text_document->document() == nullptr)
        {
            continue;
        }

        QObject *layout = text_document->document()->documentLayout();
        if (layout == nullptr)
        {
            continue;
        }

        QCoreApplication::sendPostedEvents(layout, QEvent::MetaCall);
    }
}

int inline_diff_manager_t::current_document_anchor_line(int index) const
{
    const auto &hunk = this->hunks[std::size_t(index)].hunk;
    int line = base_anchor_line_for_hunk(hunk) + this->current_document_line_delta_before(index);
    return qMax(1, line);
}

int inline_diff_manager_t::current_document_line_delta_before(int index) const
{
    const auto &hunk = this->hunks[std::size_t(index)].hunk;
    int line_delta = 0;
    for (int i = 0; i < index; ++i)
    {
        if (this->accepted.contains(i) == false)
        {
            continue;
        }

        const auto &accepted_hunk = this->hunks[std::size_t(i)].hunk;
        if (accepted_hunk.file_path != hunk.file_path)
        {
            continue;
        }

        line_delta += accepted_hunk.count_new - accepted_hunk.count_old;
    }

    return line_delta;
}

void inline_diff_manager_t::refresh_marks_for_file(const QString &file_path)
{
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        auto &entry = this->hunks[i];
        if (this->resolved.contains(int(i)) == true || entry.mark == nullptr ||
            entry.hunk.file_path != file_path)
        {
            continue;
        }

        const int anchor_line = this->current_document_anchor_line(int(i));
        if (entry.mark->lineNumber() == anchor_line)
        {
            continue;
        }

        trace_inline_diff(
            QStringLiteral("refresh_marks_for_file move index=%1 file=%2 from=%3 to=%4")
                .arg(int(i))
                .arg(file_path)
                .arg(entry.mark->lineNumber())
                .arg(anchor_line));
        entry.mark->move(anchor_line);
        entry.mark->updateMarker();
    }
}

void inline_diff_manager_t::create_marker(int index, bool attach_widgets)
{
    const auto &h = this->hunks[std::size_t(index)].hunk;
    const QString absPath = QDir(this->project_dir).absoluteFilePath(h.file_path);
    const Utils::FilePath file_path = Utils::FilePath::fromString(absPath);

    const QString tooltip_body = h.hunk_header + QStringLiteral("\n") + h.hunk_body;
    const QString tooltip =
        QStringLiteral("<b>AI diff hunk</b><br/>Use the inline widget or gutter actions."
                       "<br/>Use the action buttons to reject or bulk-resolve."
                       "<pre>%1</pre>")
            .arg(tooltip_body.toHtmlEscaped());
    const QString annotation = inline_diff_line_annotations_enabled() == true
                                   ? preview_annotation_for_hunk(h.hunk_body)
                                   : QString();
    auto *mark = new diff_text_mark_t(
        file_path, this->current_document_anchor_line(index), tooltip, annotation,
        [this, index]() { accept_hunk(index); }, [this, index]() { this->reject_hunk(index); },
        [this]() { this->accept_all(); }, [this]() { this->reject_all(); });

    this->hunks[std::size_t(index)].mark = mark;
    const QList<Core::IEditor *> editors = text_widget_editors_for_file_path(file_path);
    trace_inline_diff(QStringLiteral("create_marker index=%1 file=%2 editors=%3")
                          .arg(index)
                          .arg(file_path.toUrlishString())
                          .arg(editors.size()));
    for (Core::IEditor *editor : editors)
    {
        TextEditor::TextDocument *text_document = text_document_for_editor(editor);
        if (editor != nullptr && text_document != nullptr)
        {
            trace_inline_diff(QStringLiteral("create_marker editor index=%1 file=%2 %3")
                                  .arg(index)
                                  .arg(file_path.toUrlishString(), editor_debug_string(editor)));
            this->track_document(text_document);
        }
    }

    if (attach_widgets == false)
    {
        return;
    }

    for (Core::IEditor *editor : editors)
    {
        this->attach_hunk_widget(index, editor);
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
    const QString abs_path = QDir(this->project_dir).absoluteFilePath(h.file_path);
    const Utils::FilePath file_path = Utils::FilePath::fromString(abs_path);
    TextEditor::TextDocument *text_document = nullptr;
    const QList<Core::IEditor *> editors = text_widget_editors_for_file_path(file_path);
    for (Core::IEditor *editor : editors)
    {
        text_document = text_document_for_editor(editor);
        if (text_document != nullptr)
        {
            break;
        }
    }

    if (text_document == nullptr)
    {
        QCAI_WARN("InlineDiff",
                  QStringLiteral("Failed to accept hunk %1: no open text document for %2")
                      .arg(hunkIndex)
                      .arg(h.file_path));
        return;
    }

    QString error_message;
    const auto edit = build_hunk_text_edit(h, this->current_document_line_delta_before(hunkIndex),
                                           text_document->plainText(), &error_message);
    if (edit.has_value() == false)
    {
        QCAI_WARN("InlineDiff", QStringLiteral("Failed to accept hunk %1 in %2: %3")
                                    .arg(hunkIndex)
                                    .arg(h.file_path)
                                    .arg(error_message));
        return;
    }

    this->detach_widgets_for_file(h.file_path);
    this->detach_marks_for_file(h.file_path);
    flush_pending_layout_updates_for_file(this->project_dir, h.file_path);

    this->internal_document_change_files.insert(h.file_path);
    Utils::ChangeSet change_set;
    change_set.replace(edit->start_position, edit->end_position, edit->replacement_text);
    const bool applied = text_document->applyChangeSet(change_set);
    this->internal_document_change_files.remove(h.file_path);
    if (applied == false)
    {
        this->reattach_marks_for_file(h.file_path);
        this->reattach_widgets_for_file(h.file_path);
        QCAI_WARN("InlineDiff",
                  QStringLiteral("Failed to apply hunk %1 in %2 through editor change set")
                      .arg(hunkIndex)
                      .arg(h.file_path));
        return;
    }

    QCAI_INFO("InlineDiff",
              QStringLiteral("Accepted hunk %1 in %2").arg(hunkIndex).arg(h.file_path));

    this->resolved.insert(hunkIndex);
    this->accepted.insert(hunkIndex);
    for (auto &entry : this->hunks[std::size_t(hunkIndex)].widget_handles)
    {
        destroy_widget_handle(entry);
    }
    this->hunks[std::size_t(hunkIndex)].widget_handles.clear();
    delete this->hunks[std::size_t(hunkIndex)].mark;
    this->hunks[std::size_t(hunkIndex)].mark = nullptr;
    this->reattach_marks_for_file(h.file_path);
    this->reattach_widgets_for_file(h.file_path);
    this->refresh_marks_for_file(h.file_path);
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
    this->rejected.insert(hunkIndex);
    for (auto &entry : this->hunks[std::size_t(hunkIndex)].widget_handles)
    {
        destroy_widget_handle(entry);
    }
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

QString inline_diff_manager_t::accepted_diff() const
{
    return this->diff_for_indexes(this->accepted);
}

QString inline_diff_manager_t::rejected_diff() const
{
    return this->diff_for_indexes(this->rejected);
}

QString inline_diff_manager_t::diff_for_indexes(const QSet<int> &indexes) const
{
    QStringList patches;
    patches.reserve(qsizetype(this->hunks.size()));
    for (std::size_t i = 0; i < this->hunks.size(); ++i)
    {
        if (indexes.contains(int(i)) == false)
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
    trace_inline_diff(QStringLiteral("clear_all begin tracked_documents=%1 hunks=%2")
                          .arg(this->tracked_documents.size())
                          .arg(this->hunks.size()));
    for (const QPointer<TextEditor::TextDocument> &document :
         std::as_const(this->tracked_documents))
    {
        if (document.isNull() == false)
        {
            disconnect(document.data(), nullptr, this, nullptr);
        }
    }
    this->tracked_documents.clear();
    this->pending_mark_reattach_files.clear();
    this->pending_external_reload_invalidation_files.clear();
    this->pending_local_edit_invalidation_files.clear();
    this->internal_document_change_files.clear();
    this->reload_annotation_hidden_files.clear();
    this->annotations_hidden_by_reload = false;

    for (auto &entry : this->hunks)
    {
        for (auto &widget_handle : entry.widget_handles)
        {
            destroy_widget_handle(widget_handle);
        }
        entry.widget_handles.clear();
        delete entry.mark;
    }
    this->hunks.clear();
    this->resolved.clear();
    this->accepted.clear();
    this->rejected.clear();
    this->pending_mark_reattach_files.clear();
    this->pending_external_reload_invalidation_files.clear();
    this->reload_annotation_hidden_files.clear();
    if (this->annotations_hidden_by_reload == true)
    {
        TextEditor::TextDocument::showMarksAnnotation(kDiffCategory.id);
        this->annotations_hidden_by_reload = false;
    }
    this->project_dir.clear();
    trace_inline_diff(QStringLiteral("clear_all end"));
}

}  // namespace qcai2
