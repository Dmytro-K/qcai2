/*! Declares linked-files behavior used by AgentDockWidget. */
#pragma once

#include <QStringList>

namespace qcai2
{

class AgentDockWidget;

class AgentDockLinkedFilesController
{
public:
    explicit AgentDockLinkedFilesController(AgentDockWidget &dock);

    QStringList linkedFileCandidates() const;
    QStringList effectiveLinkedFiles() const;
    QString linkedFilesPromptContext() const;
    QString linkedFileAbsolutePath(const QString &path) const;

    void addLinkedFiles(const QStringList &paths);
    void removeSelectedLinkedFiles();
    void refreshUi();
    void clearState();

    const QStringList &manualLinkedFiles() const;
    const QStringList &ignoredLinkedFiles() const;
    void setManualLinkedFiles(const QStringList &paths);
    void setIgnoredLinkedFiles(const QStringList &paths);
    void invalidateCandidates();

private:
    QString currentProjectDir() const;
    QStringList linkedFilesFromGoalText() const;
    QString currentEditorLinkedFile() const;
    QString defaultLinkedFile() const;
    QString normalizeLinkedFilePath(const QString &path) const;

    AgentDockWidget &m_dock;
    QStringList m_manualLinkedFiles;
    QStringList m_ignoredLinkedFiles;
    QStringList m_hiddenDefaultLinkedFiles;
    mutable QString m_cachedLinkedFileRoot;
    mutable QStringList m_cachedLinkedFileCandidates;
};

}  // namespace qcai2
