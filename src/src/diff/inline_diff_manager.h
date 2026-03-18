/*! Declares helpers that show unified diff hunks as inline editor markers. */
#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

namespace TextEditor
{
class EmbeddedWidgetInterface;
class TextMark;
}  // namespace TextEditor

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
     * Returns a unified diff built from the hunks that are still unresolved.
     */
    QString remaining_diff() const;

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
        std::vector<std::unique_ptr<TextEditor::EmbeddedWidgetInterface>> widget_handles;
    };

    /**
     * Splits a unified diff into self-contained hunk entries.
     * @param unifiedDiff Unified diff text to process.
     */
    static QList<diff_hunk_t> parse_diff(const QString &unified_diff);

    /**
     * Creates the gutter marker and actions for one hunk entry.
     * @param index Index value.
     */
    void create_marker(int index);

    /** Parsed hunks for the current diff preview. */
    std::vector<hunk_entry_t> hunks;

    /** Hunk indexes that were already accepted or rejected. */
    QSet<int> resolved;

    /** Project directory used to resolve relative file paths. */
    QString project_dir;
};

}  // namespace qcai2
