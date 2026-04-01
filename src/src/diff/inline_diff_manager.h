/*! Declares helpers that show unified diff hunks as inline editor markers. */
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

namespace TextEditor
{
class BaseTextEditor;
class EmbeddedWidgetInterface;
class TextDocument;
class TextMark;
class TextEditorWidget;
}  // namespace TextEditor

class QWidget;

namespace Core
{
class IEditor;
}

namespace qcai2
{

/**
 * Parsed hunk extracted from a unified diff.
 */
struct diff_hunk_t
{
    /** Relative file path extracted from the diff header. */
    QString file_path;

    /** First affected line in the original file. */
    int start_line_old = 0;

    /** Number of lines covered in the original file. */
    int count_old = 0;

    /** First affected line in the patched file. */
    int start_line_new = 0;

    /** Number of lines covered in the patched file. */
    int count_new = 0;

    /** Raw @@ header line for the hunk. */
    QString hunk_header;

    /** Unified diff body containing context and changed lines. */
    QString hunk_body;

    /** Self-contained mini-patch with file headers for this hunk. */
    QString full_patch;
};

/**
 * Shows diff hunks as gutter markers with accept and reject actions.
 */
class inline_diff_manager_t : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a manager with no active diff markers.
     * @param parent Parent QObject that owns this instance.
     */
    explicit inline_diff_manager_t(QObject *parent = nullptr);

    /**
     * Removes any remaining markers before destruction.
     */
    ~inline_diff_manager_t() override;

    /**
     * Parses a unified diff and creates inline markers for each hunk.
     * @param unifiedDiff Diff text in unified format.
     * @param projectDir Project directory used to resolve relative paths.
     */
    void show_diff(const QString &unified_diff, const QString &project_dir);

    /**
     * Removes all markers and forgets the current diff.
     */
    void clear_all();

    /**
     * Applies one hunk and clears its marker.
     * @param hunkIndex Index in the parsed hunk list.
     */
    void accept_hunk(int hunk_index);

    /**
     * Rejects one hunk and clears its marker.
     * @param hunkIndex Index in the parsed hunk list.
     */
    void reject_hunk(int hunk_index);

    /**
     * Applies every unresolved hunk.
     */
    void accept_all();

    /**
     * Rejects every unresolved hunk.
     */
    void reject_all();

    /**
     * Returns true when at least one parsed hunk is available.
     */
    bool has_hunks() const
    {
        return !this->hunks.empty();
    }

    /**
     * Returns the number of parsed hunks in the current diff.
     */
    qsizetype hunk_count() const
    {
        return qsizetype(this->hunks.size());
    }

    /**
     * Returns the number of hunks already accepted or rejected.
     */
    qsizetype resolved_count() const
    {
        return this->resolved.size();
    }

    /**
     * Returns the number of hunks accepted in the current review round.
     */
    qsizetype accepted_count() const
    {
        return this->accepted.size();
    }

    /**
     * Returns the number of hunks rejected in the current review round.
     */
    qsizetype rejected_count() const
    {
        return this->rejected.size();
    }

    /**
     * Returns a unified diff built from the hunks that are still unresolved.
     */
    QString remaining_diff() const;

    /**
     * Returns a unified diff built from the hunks accepted in the current review round.
     */
    QString accepted_diff() const;

    /**
     * Returns a unified diff built from the hunks rejected in the current review round.
     */
    QString rejected_diff() const;

signals:
    /**
     * Emitted after one hunk is applied to disk.
     * @param index Index value.
     * @param filePath Path of the affected file.
     */
    void hunk_accepted(int index, const QString &file_path);

    /**
     * Emitted after one hunk is rejected and removed from the gutter.
     * @param index Index value.
     * @param filePath Path of the affected file.
     */
    void hunk_rejected(int index, const QString &file_path);

    /**
     * Emitted when the unresolved diff changes after accepting or rejecting hunks.
     * @param diff Unified diff containing only unresolved hunks.
     */
    void diff_changed(const QString &diff);

    /**
     * Emitted after every hunk in the current diff is resolved.
     */
    void all_resolved();

private:
    struct widget_handle_entry_t
    {
        QPointer<Core::IEditor> editor;
        QPointer<TextEditor::TextEditorWidget> editor_widget;
        QPointer<TextEditor::TextDocument> document;
        QPointer<QWidget> widget;
        std::unique_ptr<TextEditor::EmbeddedWidgetInterface> handle;
    };

    /**
     * Parsed hunk plus its optional gutter marker.
     */
    struct hunk_entry_t
    {
        /** Parsed diff payload for the entry. */
        diff_hunk_t hunk;

        /** Live marker shown in the editor gutter, or null when resolved. */
        TextEditor::TextMark *mark = nullptr;

        /** Inline widgets inserted into open editor instances for this hunk. */
        std::vector<widget_handle_entry_t> widget_handles;
    };

    /**
     * Splits a unified diff into self-contained hunk entries.
     * @param unifiedDiff Unified diff text to process.
     */
    static QList<diff_hunk_t> parse_diff(const QString &unified_diff);

    /**
     * Creates the gutter marker and actions for one hunk entry.
     * @param index Index value.
     * @param attach_widgets When true, also recreates inline preview widgets for open editors.
     */
    void create_marker(int index, bool attach_widgets = true);

    /**
     * Adds the inline preview widget for one hunk into one opened editor that
     * exposes a TextEditorWidget.
     * @param index Hunk index.
     * @param editor Opened editor instance.
     */
    void attach_hunk_widget(int index, Core::IEditor *editor);

    /**
     * Reattaches inline preview widgets when a matching editor is opened later.
     * @param editor Newly opened editor instance.
     */
    void attach_widgets_for_editor(Core::IEditor *editor);

    /**
     * Starts tracking one text document so inline diff state can react safely to
     * document reloads.
     * @param document Text document that hosts diff markers.
     */
    void track_document(TextEditor::TextDocument *document);

    /**
     * Returns true when one file still has at least one unresolved hunk.
     * @param file_path Relative file path to inspect.
     */
    bool has_unresolved_hunks_for_file(const QString &file_path) const;

    /**
     * Temporarily hides inline annotations while one or more tracked documents
     * are reloading so Qt Creator does not render stale annotation state.
     * @param file_path Relative file path currently entering reload.
     */
    void suspend_annotations_for_reload(const QString &file_path);

    /**
     * Restores inline annotations after reload-safe cleanup finished.
     * @param file_path Relative file path whose reload lifecycle finished.
     */
    void resume_annotations_after_reload(const QString &file_path);

    /**
     * Removes inline widgets attached to one text document before reload mutates
     * the underlying block layout.
     * @param document Text document being reloaded.
     */
    void detach_widgets_for_document(TextEditor::TextDocument *document);

    /**
     * Recreates inline widgets for one text document after reload completed.
     * @param document Text document that finished reloading.
     */
    void reattach_widgets_for_document(TextEditor::TextDocument *document);

    /**
     * Removes inline widgets for all unresolved hunks that belong to one file.
     * @param file_path Relative file path whose widgets should be removed.
     */
    void detach_widgets_for_file(const QString &file_path);

    /**
     * Recreates inline widgets for all unresolved hunks that belong to one file.
     * @param file_path Relative file path whose widgets should be recreated.
     */
    void reattach_widgets_for_file(const QString &file_path);

    /**
     * Removes gutter marks for all unresolved hunks that belong to one file.
     * @param file_path Relative file path whose marks should be removed.
     */
    void detach_marks_for_file(const QString &file_path);

    /**
     * Recreates gutter marks for all unresolved hunks that belong to one file.
     * @param file_path Relative file path whose marks should be recreated.
     */
    void reattach_marks_for_file(const QString &file_path);

    /**
     * Invalidates unresolved hunks for one file after document changes made their anchors and
     * patch context stale.
     * @param file_path Relative file path whose unresolved hunks should be rejected.
     */
    void invalidate_hunks_for_file(const QString &file_path);

    /**
     * Returns the best anchor line for one hunk in the current partially-applied
     * document state.
     * @param index Hunk index.
     */
    int current_document_anchor_line(int index) const;

    /**
     * Returns the cumulative line-count delta from earlier accepted hunks in the same file.
     * @param index Current hunk index.
     */
    int current_document_line_delta_before(int index) const;

    /**
     * Repositions unresolved marks for one file after earlier accepted hunks changed
     * the current document line offsets.
     * @param file_path Relative file path of the affected file.
     */
    void refresh_marks_for_file(const QString &file_path);

    /**
     * Synchronously removes one embedded preview widget so Qt Creator no longer keeps
     * stale block-layout references around reload or hunk resolution.
     * @param entry Stored handle entry to clear.
     */
    static void destroy_widget_handle(widget_handle_entry_t &entry);

    /**
     * Builds a unified diff from the provided hunk indexes.
     * @param indexes Resolved hunk indexes to include.
     */
    QString diff_for_indexes(const QSet<int> &indexes) const;

    /** Parsed hunks for the current diff preview. */
    std::vector<hunk_entry_t> hunks;

    /** Hunk indexes that were already accepted or rejected. */
    QSet<int> resolved;

    /** Hunk indexes that were accepted in the current review round. */
    QSet<int> accepted;

    /** Hunk indexes that were rejected in the current review round. */
    QSet<int> rejected;

    /** Project directory used to resolve relative file paths. */
    QString project_dir;

    /** Text documents currently tracked for reload-safe inline widget lifecycle. */
    std::vector<QPointer<TextEditor::TextDocument>> tracked_documents;

    /** Files whose unresolved gutter marks must be recreated after the next document reload. */
    QSet<QString> pending_mark_reattach_files;

    /** Files whose unresolved hunks must be invalidated after an external reload finishes. */
    QSet<QString> pending_external_reload_invalidation_files;

    /** Files whose local non-plugin edits must invalidate remaining unresolved hunks. */
    QSet<QString> pending_local_edit_invalidation_files;

    /** Files currently being edited internally while accepting one diff hunk. */
    QSet<QString> internal_document_change_files;

    /** Files whose reload currently keeps inline annotations temporarily hidden. */
    QSet<QString> reload_annotation_hidden_files;

    /** Tracks whether this manager hid annotations itself and must restore them later. */
    bool annotations_hidden_by_reload = false;

    /** Monotonic counter incremented on each show_diff call to discard stale reload invalidations.
     */
    int diff_generation = 0;
};

}  // namespace qcai2
