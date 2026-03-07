/*! Declares helpers that show unified diff hunks as inline editor markers. */
#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

namespace TextEditor
{
class TextMark;
}

namespace qcai2
{

/**
 * Parsed hunk extracted from a unified diff.
 */
struct DiffHunk
{
    /** Relative file path extracted from the diff header. */
    QString filePath;
    /** First affected line in the original file. */
    int startLineOld = 0;
    /** Number of lines covered in the original file. */
    int countOld = 0;
    /** First affected line in the patched file. */
    int startLineNew = 0;
    /** Number of lines covered in the patched file. */
    int countNew = 0;
    /** Raw @@ header line for the hunk. */
    QString hunkHeader;
    /** Unified diff body containing context and changed lines. */
    QString hunkBody;
    /** Self-contained mini-patch with file headers for this hunk. */
    QString fullPatch;
};

/**
 * Shows diff hunks as gutter markers with accept and reject actions.
 */
class InlineDiffManager : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a manager with no active diff markers.
     * @param parent Parent QObject that owns this instance.
     */
    explicit InlineDiffManager(QObject *parent = nullptr);
    /**
     * Removes any remaining markers before destruction.
     */
    ~InlineDiffManager() override;

    /**
     * Parses a unified diff and creates inline markers for each hunk.
     * @param unifiedDiff Diff text in unified format.
     * @param projectDir Project directory used to resolve relative paths.
     */
    void showDiff(const QString &unifiedDiff, const QString &projectDir);

    /**
     * Removes all markers and forgets the current diff.
     */
    void clearAll();

    /**
     * Applies one hunk and clears its marker.
     * @param hunkIndex Index in the parsed hunk list.
     */
    void acceptHunk(int hunkIndex);

    /**
     * Rejects one hunk and clears its marker.
     * @param hunkIndex Index in the parsed hunk list.
     */
    void rejectHunk(int hunkIndex);

    /**
     * Applies every unresolved hunk.
     */
    void acceptAll();

    /**
     * Rejects every unresolved hunk.
     */
    void rejectAll();

    /**
     * Returns true when at least one parsed hunk is available.
     */
    bool hasHunks() const
    {
        return !m_hunks.isEmpty();
    }
    /**
     * Returns the number of parsed hunks in the current diff.
     */
    qsizetype hunkCount() const
    {
        return m_hunks.size();
    }
    /**
     * Returns the number of hunks already accepted or rejected.
     */
    qsizetype resolvedCount() const
    {
        return m_resolved.size();
    }

signals:
    /**
     * Emitted after one hunk is applied to disk.
     * @param index Index value.
     * @param filePath Path of the affected file.
     */
    void hunkAccepted(int index, const QString &filePath);
    /**
     * Emitted after one hunk is rejected and removed from the gutter.
     * @param index Index value.
     * @param filePath Path of the affected file.
     */
    void hunkRejected(int index, const QString &filePath);
    /**
     * Emitted after every hunk in the current diff is resolved.
     */
    void allResolved();

private:
    /**
     * Parsed hunk plus its optional gutter marker.
     */
    struct HunkEntry
    {
        /** Parsed diff payload for the entry. */
        DiffHunk hunk;
        /** Live marker shown in the editor gutter, or null when resolved. */
        TextEditor::TextMark *mark = nullptr;
    };

    /**
     * Splits a unified diff into self-contained hunk entries.
     * @param unifiedDiff Unified diff text to process.
     */
    static QList<DiffHunk> parseDiff(const QString &unifiedDiff);
    /**
     * Creates the gutter marker and actions for one hunk entry.
     * @param index Index value.
     */
    void createMarker(int index);

    /** Parsed hunks for the current diff preview. */
    QList<HunkEntry> m_hunks;
    /** Hunk indexes that were already accepted or rejected. */
    QSet<int> m_resolved;
    /** Project directory used to resolve relative file paths. */
    QString m_projectDir;
};

}  // namespace qcai2
