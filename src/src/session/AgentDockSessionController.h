/*! Declares project-session persistence used by AgentDockWidget. */
#pragma once

#include <QString>
#include <QStringList>

#include <memory>

class QFileSystemWatcher;
class QTimer;

namespace qcai2
{

class AgentDockWidget;

class AgentDockSessionController
{
public:
    explicit AgentDockSessionController(AgentDockWidget &dock);
    ~AgentDockSessionController();

    void saveChat();
    void restoreChat();
    void refreshProjectSelector();
    void switchProjectContext(const QString &projectFilePath);

    QString currentProjectFilePath() const;
    QString currentProjectStorageFilePath() const;
    QString legacyProjectStorageFilePath() const;
    QStringList currentSessionWatchPaths() const;
    QString currentProjectDir() const;

    void applyProjectUiDefaults();
    void migrateLegacySessionFiles();
    void updateSessionFileWatcher();
    void reloadSessionFromDisk();

private:
    QString projectDirForPath(const QString &projectFilePath) const;

    AgentDockWidget &m_dock;
    QString m_activeProjectFilePath;
    std::unique_ptr<QFileSystemWatcher> m_sessionFileWatcher;
    std::unique_ptr<QTimer> m_sessionReloadTimer;
    bool m_sessionStoragePresent = false;
};

}  // namespace qcai2
