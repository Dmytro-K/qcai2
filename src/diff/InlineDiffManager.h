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

// Parsed hunk from a unified diff
struct DiffHunk
{
    QString filePath;  // relative path (from diff header)
    int startLineOld = 0;
    int countOld = 0;
    int startLineNew = 0;
    int countNew = 0;
    QString hunkHeader;  // @@ ... @@ line
    QString hunkBody;    // +/- / context lines
    QString fullPatch;   // complete mini-patch for this hunk (with file headers)
};

// Shows inline diff markers in Qt Creator editors.
// Each hunk gets a TextMark with Accept/Reject actions in the gutter.
class InlineDiffManager : public QObject
{
    Q_OBJECT
public:
    explicit InlineDiffManager(QObject *parent = nullptr);
    ~InlineDiffManager() override;

    // Parse unified diff and show markers in editors
    void showDiff(const QString &unifiedDiff, const QString &projectDir);

    // Remove all markers
    void clearAll();

    // Accept a specific hunk (applies it to the file)
    void acceptHunk(int hunkIndex);

    // Reject a specific hunk (removes its marker)
    void rejectHunk(int hunkIndex);

    // Accept all remaining hunks
    void acceptAll();

    // Reject all remaining hunks
    void rejectAll();

    bool hasHunks() const
    {
        return !m_hunks.isEmpty();
    }
    qsizetype hunkCount() const
    {
        return m_hunks.size();
    }
    qsizetype resolvedCount() const
    {
        return m_resolved.size();
    }

signals:
    void hunkAccepted(int index, const QString &filePath);
    void hunkRejected(int index, const QString &filePath);
    void allResolved();

private:
    struct HunkEntry
    {
        DiffHunk hunk;
        TextEditor::TextMark *mark = nullptr;
    };

    static QList<DiffHunk> parseDiff(const QString &unifiedDiff);
    void createMarker(int index);

    QList<HunkEntry> m_hunks;
    QSet<int> m_resolved;
    QString m_projectDir;
};

}  // namespace qcai2
