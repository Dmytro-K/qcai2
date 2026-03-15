/*! Implements project-session persistence extracted from AgentDockWidget. */

#include "AgentDockSessionController.h"

#include "../ui/AgentDockWidget.h"

#include "../linked_files/AgentDockLinkedFilesController.h"

#include "../settings/Settings.h"
#include "../util/Logger.h"
#include "../util/Migration.h"

#include <qtmcp/ServerDefinition.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>

namespace qcai2
{

namespace
{

QString startupProjectFilePath()
{
    if (auto *project = ProjectExplorer::ProjectManager::startupProject();
        ((project != nullptr) == true))
    {
        return project->projectFilePath().toUrlishString();
    }
    return {};
}

QString legacySanitizedSessionFileName(const QString &projectFilePath)
{
    QString baseName = QFileInfo(projectFilePath).completeBaseName();
    if (baseName.isEmpty() == true)
    {
        baseName = QStringLiteral("session");
    }

    baseName.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-])")),
                     QStringLiteral("_"));
    return QStringLiteral("%1.json").arg(baseName);
}

void selectEffortValue(QComboBox *combo, const QString &value, int fallbackIndex)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallbackIndex);
}

bool writeContextTextFile(const QString &path, const QString &content, const QString &description)
{
    if (content.isEmpty() == true)
    {
        QFile::remove(path);
        return true;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for writing: %2").arg(description, path));
        return false;
    }

    file.write(content.toUtf8());
    if (file.commit() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to commit %1: %2").arg(description, path));
        return false;
    }

    return true;
}

QString readContextTextFile(const QString &path, const QString &description, bool *ok = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (((ok != nullptr) == true))
        {
            *ok = false;
        }
        return {};
    }

    if (file.open(QIODevice::ReadOnly) == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open %1 for reading: %2").arg(description, path));
        if (((ok != nullptr) == true))
        {
            *ok = false;
        }
        return {};
    }

    if (((ok != nullptr) == true))
    {
        *ok = true;
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

AgentDockSessionController::AgentDockSessionController(AgentDockWidget &dock)
    : m_dock(dock), m_sessionFileWatcher(std::make_unique<QFileSystemWatcher>()),
      m_sessionReloadTimer(std::make_unique<QTimer>())
{
    m_sessionReloadTimer->setSingleShot(true);
    m_sessionReloadTimer->setInterval(150);
    QObject::connect(m_sessionFileWatcher.get(), &QFileSystemWatcher::fileChanged, &m_dock,
                     [this](const QString &) {
                         updateSessionFileWatcher();
                         m_sessionReloadTimer->start();
                     });
    QObject::connect(m_sessionFileWatcher.get(), &QFileSystemWatcher::directoryChanged, &m_dock,
                     [this](const QString &) {
                         updateSessionFileWatcher();
                         m_sessionReloadTimer->start();
                     });
    QObject::connect(m_sessionReloadTimer.get(), &QTimer::timeout, &m_dock,
                     [this]() { reloadSessionFromDisk(); });
}

AgentDockSessionController::~AgentDockSessionController() = default;

void AgentDockSessionController::saveChat()
{
    migrateLegacySessionFiles();
    const QString storagePath = currentProjectStorageFilePath();
    if (storagePath.isEmpty() == true)
    {
        return;
    }
    const QString goalPath = Migration::projectGoalFilePath(storagePath);
    const QString logPath = Migration::projectActionsLogFilePath(storagePath);

    const QString persistedMarkdown = m_dock.currentLogMarkdown();
    const QString goalText = m_dock.m_goalEdit->toPlainText();
    QStringList planItems;
    for (int i = 0; ((i < m_dock.m_planList->count()) == true); ++i)
    {
        planItems.append(m_dock.m_planList->item(i)->text());
    }
    const auto &cfg = settings();
    const bool uiStateDiffers =
        m_dock.m_modeCombo->currentData().toString() != QStringLiteral("agent") ||
        m_dock.m_modelCombo->currentText().trimmed() != cfg.modelName ||
        m_dock.m_reasoningCombo->currentData().toString() != cfg.reasoningEffort ||
        m_dock.m_thinkingCombo->currentData().toString() != cfg.thinkingLevel ||
        m_dock.m_dryRunCheck->isChecked() != cfg.dryRunDefault;

    const bool hasContent = !goalText.trimmed().isEmpty() ||
                            !persistedMarkdown.trimmed().isEmpty() ||
                            !m_dock.m_currentDiff.isEmpty() || !planItems.isEmpty() ||
                            !m_dock.m_linkedFilesController->manualLinkedFiles().isEmpty() ||
                            !m_dock.m_linkedFilesController->ignoredLinkedFiles().isEmpty() ||
                            !m_projectMcpServers.isEmpty() || uiStateDiffers;

    if (((!hasContent) == true))
    {
        QFile::remove(storagePath);
        QFile::remove(goalPath);
        QFile::remove(logPath);
        updateSessionFileWatcher();
        return;
    }

    const QFileInfo storageInfo(storagePath);
    if (((!QDir().mkpath(storageInfo.absolutePath())) == true))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create project context dir: %1")
                              .arg(storageInfo.absolutePath()));
        return;
    }

    if (((!writeContextTextFile(goalPath, goalText, QStringLiteral("project goal file")) ||
          !writeContextTextFile(logPath, persistedMarkdown,
                                QStringLiteral("project Actions Log markdown file"))) == true))
    {
        return;
    }

    QJsonObject root;
    root[QStringLiteral("diff")] = m_dock.m_currentDiff;
    root[QStringLiteral("status")] = m_dock.m_statusLabel->text();
    root[QStringLiteral("activeTab")] = m_dock.m_tabs->currentIndex();
    root[QStringLiteral("mode")] = m_dock.m_modeCombo->currentData().toString();
    root[QStringLiteral("model")] = m_dock.m_modelCombo->currentText().trimmed();
    root[QStringLiteral("reasoningEffort")] = m_dock.m_reasoningCombo->currentData().toString();
    root[QStringLiteral("thinkingLevel")] = m_dock.m_thinkingCombo->currentData().toString();
    root[QStringLiteral("dryRun")] = m_dock.m_dryRunCheck->isChecked();
    root[QStringLiteral("linkedFiles")] =
        QJsonArray::fromStringList(m_dock.m_linkedFilesController->manualLinkedFiles());
    root[QStringLiteral("ignoredLinkedFiles")] =
        QJsonArray::fromStringList(m_dock.m_linkedFilesController->ignoredLinkedFiles());
    if (((!m_projectMcpServers.isEmpty()) == true))
    {
        root[QStringLiteral("mcpServers")] = qtmcp::serverDefinitionsToJson(m_projectMcpServers);
    }
    Migration::stampProjectState(root);

    QJsonArray planJson;
    for (const QString &item : planItems)
    {
        planJson.append(item);
    }
    root[QStringLiteral("plan")] = planJson;

    QSaveFile file(storagePath);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to open project context file for writing: %1")
                              .arg(storagePath));
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() == false)
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to commit project context file: %1").arg(storagePath));
    }

    updateSessionFileWatcher();
}

void AgentDockSessionController::restoreChat()
{
    migrateLegacySessionFiles();
    const QString storagePath = currentProjectStorageFilePath();
    if (storagePath.isEmpty() == true)
    {
        return;
    }
    QString migrationError;
    if (Migration::migrateProjectState(storagePath, &migrationError) == false)
    {
        if (migrationError.isEmpty() == false)
        {
            QCAI_WARN("Dock", migrationError);
        }
        return;
    }

    const QString goalPath = Migration::projectGoalFilePath(storagePath);
    const QString logPath = Migration::projectActionsLogFilePath(storagePath);

    QFile file(storagePath);
    if (file.exists() == false)
    {
        return;
    }
    if (file.open(QIODevice::ReadOnly) == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to open project context file for reading: %1")
                              .arg(storagePath));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject() == false)
    {
        QCAI_WARN("Dock", QStringLiteral("Invalid project context JSON: %1").arg(storagePath));
        return;
    }

    const QJsonObject root = doc.object();

    {
        const QSignalBlocker modeBlocker(m_dock.m_modeCombo);
        const QSignalBlocker modelBlocker(m_dock.m_modelCombo);
        const QSignalBlocker reasoningBlocker(m_dock.m_reasoningCombo);
        const QSignalBlocker thinkingBlocker(m_dock.m_thinkingCombo);
        const QSignalBlocker dryRunBlocker(m_dock.m_dryRunCheck);

        selectEffortValue(m_dock.m_reasoningCombo,
                          root.value(QStringLiteral("reasoningEffort"))
                              .toString(m_dock.m_reasoningCombo->currentData().toString()),
                          m_dock.m_reasoningCombo->currentIndex());
        selectEffortValue(m_dock.m_thinkingCombo,
                          root.value(QStringLiteral("thinkingLevel"))
                              .toString(m_dock.m_thinkingCombo->currentData().toString()),
                          m_dock.m_thinkingCombo->currentIndex());

        const QString savedMode = root.value(QStringLiteral("mode")).toString();
        const int modeIndex = m_dock.m_modeCombo->findData(savedMode);
        if (((modeIndex >= 0) == true))
        {
            m_dock.m_modeCombo->setCurrentIndex(modeIndex);
        }

        const QString savedModel = root.value(QStringLiteral("model")).toString().trimmed();
        if (savedModel.isEmpty() == false)
        {
            m_dock.m_modelCombo->setCurrentText(savedModel);
        }

        if (((root.contains(QStringLiteral("dryRun"))) == true))
        {
            m_dock.m_dryRunCheck->setChecked(root.value(QStringLiteral("dryRun")).toBool());
        }
    }

    bool goalLoadedFromFile = false;
    const QString goal =
        readContextTextFile(goalPath, QStringLiteral("project goal file"), &goalLoadedFromFile);
    const QString legacyGoal = root.value(QStringLiteral("goal")).toString();
    const QString restoredGoal = goalLoadedFromFile ? goal : legacyGoal;
    if (restoredGoal.isEmpty() == false)
    {
        m_dock.m_goalEdit->setPlainText(restoredGoal);
    }

    bool logLoadedFromFile = false;
    const QString logMarkdown = readContextTextFile(
        logPath, QStringLiteral("project Actions Log markdown file"), &logLoadedFromFile);
    const QString legacyLogMarkdown = root.value(QStringLiteral("logMarkdown")).toString();
    const QString restoredLogMarkdown = logLoadedFromFile ? logMarkdown : legacyLogMarkdown;
    if (restoredLogMarkdown.isEmpty() == false)
    {
        m_dock.m_logMarkdown = restoredLogMarkdown;
        int fenceCount = 0;
        const auto lines = m_dock.m_logMarkdown.split('\n');
        for (const auto &line : lines)
        {
            if (line.trimmed().startsWith(QStringLiteral("```")))
            {
                ++fenceCount;
            }
        }
        if (((fenceCount % 2 != 0) == true))
        {
            m_dock.m_logMarkdown += QStringLiteral("\n```\n");
        }
        m_dock.m_streamingMarkdown.clear();
        m_dock.renderLog();
    }
    else
    {
        const QString log = root.value(QStringLiteral("log")).toString();
        if (log.isEmpty() == false)
        {
            m_dock.m_logMarkdown = log;
            m_dock.renderLog();
        }
    }

    m_dock.m_currentDiff = root.value(QStringLiteral("diff")).toString();
    if (((!m_dock.m_currentDiff.isEmpty()) == true))
    {
        m_dock.syncDiffUi(m_dock.m_currentDiff, false, true);
    }

    const QString status = root.value(QStringLiteral("status")).toString();
    if (status.isEmpty() == false)
    {
        m_dock.m_statusLabel->setText(status);
    }

    const QJsonArray planItems = root.value(QStringLiteral("plan")).toArray();
    for (const auto &item : planItems)
    {
        if (item.isString())
        {
            m_dock.m_planList->addItem(item.toString());
        }
    }

    QStringList manualLinkedFiles;
    const QJsonArray linkedFiles = root.value(QStringLiteral("linkedFiles")).toArray();
    for (const QJsonValue &value : linkedFiles)
    {
        if (value.isString())
        {
            manualLinkedFiles.append(value.toString());
        }
    }
    QStringList ignoredLinkedFilesList;
    QJsonArray ignoredLinkedFiles = root.value(QStringLiteral("ignoredLinkedFiles")).toArray();
    if (ignoredLinkedFiles.isEmpty() == true)
    {
        ignoredLinkedFiles = root.value(QStringLiteral("excludedDefaultLinkedFiles")).toArray();
    }
    for (const QJsonValue &value : ignoredLinkedFiles)
    {
        if (value.isString())
        {
            ignoredLinkedFilesList.append(value.toString());
        }
    }
    m_dock.m_linkedFilesController->setManualLinkedFiles(manualLinkedFiles);
    m_dock.m_linkedFilesController->setIgnoredLinkedFiles(ignoredLinkedFilesList);
    m_dock.m_linkedFilesController->invalidateCandidates();
    m_dock.m_linkedFilesController->refreshUi();

    m_projectMcpServers.clear();
    const QJsonValue mcpServersValue = root.value(QStringLiteral("mcpServers"));
    if (((!mcpServersValue.isUndefined() && !mcpServersValue.isNull()) == true))
    {
        if (mcpServersValue.isObject() == false)
        {
            QCAI_WARN("Dock",
                      QStringLiteral("Project session key 'mcpServers' must be an object."));
        }
        else
        {
            QString mcpError;
            if (((!qtmcp::serverDefinitionsFromJson(mcpServersValue.toObject(),
                                                    &m_projectMcpServers, &mcpError)) == true))
            {
                QCAI_WARN("Dock",
                          QStringLiteral("Failed to parse project MCP servers: %1").arg(mcpError));
                m_projectMcpServers.clear();
            }
        }
    }

    const int tab = root.value(QStringLiteral("activeTab")).toInt(0);
    if (((tab >= 0 && tab < m_dock.m_tabs->count()) == true))
    {
        m_dock.m_tabs->setCurrentIndex(tab);
    }

    updateSessionFileWatcher();
}

void AgentDockSessionController::refreshProjectSelector()
{
    if (((m_dock.m_projectCombo == nullptr) == true))
    {
        return;
    }

    QString desiredProject = startupProjectFilePath();
    if (desiredProject.isEmpty() == true)
    {
        if (auto *ctx = m_dock.m_controller->editorContext(); ((ctx != nullptr) == true))
        {
            desiredProject = ctx->selectedProjectFilePath();
        }
    }
    if (desiredProject.isEmpty() == true)
    {
        desiredProject = m_activeProjectFilePath;
    }

    const QList<EditorContext::ProjectInfo> projects =
        (m_dock.m_controller->editorContext() != nullptr)
            ? m_dock.m_controller->editorContext()->openProjects()
            : QList<EditorContext::ProjectInfo>{};

    const QSignalBlocker blocker(m_dock.m_projectCombo);
    m_dock.m_projectCombo->clear();

    if (projects.isEmpty())
    {
        m_dock.m_projectCombo->addItem(QObject::tr("No open projects"), QString());
        m_dock.m_projectCombo->setEnabled(false);
        switchProjectContext(QString());
        m_dock.updateRunState(m_dock.m_stopBtn->isEnabled());
        return;
    }

    QSet<QString> duplicateNames;
    QSet<QString> seenNames;
    for (const auto &project : projects)
    {
        if (seenNames.contains(project.projectName))
        {
            duplicateNames.insert(project.projectName);
        }
        seenNames.insert(project.projectName);
    }

    int selectedIndex = -1;
    for (int i = 0; i < projects.size(); ++i)
    {
        const auto &project = projects.at(i);
        QString label = project.projectName;
        if (duplicateNames.contains(project.projectName))
        {
            label = QStringLiteral("%1 — %2").arg(project.projectName,
                                                  QFileInfo(project.projectDir).fileName());
        }
        m_dock.m_projectCombo->addItem(label, project.projectFilePath);
        m_dock.m_projectCombo->setItemData(i, project.projectDir, Qt::ToolTipRole);
        if (project.projectFilePath == desiredProject)
        {
            selectedIndex = i;
        }
    }

    if (selectedIndex < 0)
    {
        selectedIndex = 0;
    }

    m_dock.m_projectCombo->setEnabled(true);
    m_dock.m_projectCombo->setCurrentIndex(selectedIndex);

    const QString selectedProject = m_dock.m_projectCombo->currentData().toString();
    if (selectedProject != m_activeProjectFilePath)
    {
        switchProjectContext(selectedProject);
    }
    m_dock.updateRunState(m_dock.m_stopBtn->isEnabled());
}

void AgentDockSessionController::switchProjectContext(const QString &projectFilePath)
{
    if (projectFilePath == m_activeProjectFilePath)
    {
        return;
    }

    if (!m_activeProjectFilePath.isEmpty())
    {
        saveChat();
    }

    m_dock.clearChatState();
    applyProjectUiDefaults();
    m_dock.m_linkedFilesController->invalidateCandidates();
    m_activeProjectFilePath = projectFilePath;
    m_projectMcpServers.clear();

    if (auto *ctx = m_dock.m_controller->editorContext())
    {
        ctx->setSelectedProjectFilePath(projectFilePath);
    }

    restoreChat();
    updateSessionFileWatcher();
}

QString AgentDockSessionController::currentProjectFilePath() const
{
    return m_activeProjectFilePath;
}

QString AgentDockSessionController::currentProjectStorageFilePath() const
{
    if (m_activeProjectFilePath.isEmpty())
    {
        return {};
    }

    const QString projectDir = projectDirForPath(m_activeProjectFilePath);
    if (projectDir.isEmpty())
    {
        return {};
    }

    return QDir(projectDir).filePath(QStringLiteral(".qcai2/session.json"));
}

QString AgentDockSessionController::legacyProjectStorageFilePath() const
{
    if (m_activeProjectFilePath.isEmpty())
    {
        return {};
    }

    const QString projectDir = projectDirForPath(m_activeProjectFilePath);
    if (projectDir.isEmpty())
    {
        return {};
    }

    return QDir(projectDir)
        .filePath(QStringLiteral(".qcai2/%1")
                      .arg(legacySanitizedSessionFileName(m_activeProjectFilePath)));
}

QStringList AgentDockSessionController::currentSessionWatchPaths() const
{
    const QString storagePath = currentProjectStorageFilePath();
    if (storagePath.isEmpty())
    {
        return {};
    }

    QStringList paths;
    const QString goalPath = Migration::projectGoalFilePath(storagePath);
    const QString logPath = Migration::projectActionsLogFilePath(storagePath);
    const QFileInfo storageInfo(storagePath);
    const QString sessionDirPath = storageInfo.absolutePath();

    const auto appendIfExists = [&paths](const QString &path) {
        if (!path.isEmpty() && QFileInfo::exists(path) && !paths.contains(path))
        {
            paths.append(path);
        }
    };

    if (QDir(sessionDirPath).exists())
    {
        paths.append(sessionDirPath);
    }
    else
    {
        const QString projectDir = currentProjectDir();
        if (!projectDir.isEmpty())
        {
            paths.append(projectDir);
        }
    }

    appendIfExists(storagePath);
    appendIfExists(goalPath);
    appendIfExists(logPath);
    return paths;
}

QString AgentDockSessionController::currentProjectDir() const
{
    return projectDirForPath(m_activeProjectFilePath);
}

const qtmcp::ServerDefinitions &AgentDockSessionController::projectMcpServers() const
{
    return m_projectMcpServers;
}

void AgentDockSessionController::applyProjectUiDefaults()
{
    const auto &cfg = settings();
    const QSignalBlocker modeBlocker(m_dock.m_modeCombo);
    const QSignalBlocker modelBlocker(m_dock.m_modelCombo);
    const QSignalBlocker reasoningBlocker(m_dock.m_reasoningCombo);
    const QSignalBlocker thinkingBlocker(m_dock.m_thinkingCombo);
    const QSignalBlocker dryRunBlocker(m_dock.m_dryRunCheck);

    const int modeIndex = m_dock.m_modeCombo->findData(QStringLiteral("agent"));
    m_dock.m_modeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    m_dock.m_modelCombo->setCurrentText(cfg.modelName);
    selectEffortValue(m_dock.m_reasoningCombo, cfg.reasoningEffort, 2);
    selectEffortValue(m_dock.m_thinkingCombo, cfg.thinkingLevel, 2);
    m_dock.m_dryRunCheck->setChecked(cfg.dryRunDefault);
}

void AgentDockSessionController::migrateLegacySessionFiles()
{
    const QString storagePath = currentProjectStorageFilePath();
    const QString legacyStoragePath = legacyProjectStorageFilePath();
    if (storagePath.isEmpty() || legacyStoragePath.isEmpty() || storagePath == legacyStoragePath)
    {
        return;
    }

    const QString goalPath = Migration::projectGoalFilePath(storagePath);
    const QString logPath = Migration::projectActionsLogFilePath(storagePath);
    const QString legacyGoalPath = Migration::projectGoalFilePath(legacyStoragePath);
    const QString legacyLogPath = Migration::projectActionsLogFilePath(legacyStoragePath);

    const bool hasCurrentFiles = QFileInfo::exists(storagePath) || QFileInfo::exists(goalPath) ||
                                 QFileInfo::exists(logPath);
    const bool hasLegacyFiles = QFileInfo::exists(legacyStoragePath) ||
                                QFileInfo::exists(legacyGoalPath) ||
                                QFileInfo::exists(legacyLogPath);
    if (hasCurrentFiles || !hasLegacyFiles)
    {
        return;
    }

    const QFileInfo storageInfo(storagePath);
    if (!QDir().mkpath(storageInfo.absolutePath()))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create project context dir: %1")
                              .arg(storageInfo.absolutePath()));
        return;
    }

    const auto renameIfExists = [](const QString &from, const QString &to, const QString &label) {
        if (!QFileInfo::exists(from))
        {
            return true;
        }
        if (QFile::exists(to))
        {
            return false;
        }
        if (QFile::rename(from, to))
        {
            return true;
        }

        QCAI_WARN("Dock",
                  QStringLiteral("Failed to rename %1 from %2 to %3").arg(label, from, to));
        return false;
    };

    const bool movedGoal =
        renameIfExists(legacyGoalPath, goalPath, QStringLiteral("legacy project goal file"));
    const bool movedLog =
        renameIfExists(legacyLogPath, logPath, QStringLiteral("legacy project Actions Log file"));
    const bool movedStorage = renameIfExists(legacyStoragePath, storagePath,
                                             QStringLiteral("legacy project context file"));

    if (!(movedGoal && movedLog && movedStorage))
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to fully migrate legacy session file names for %1")
                      .arg(m_activeProjectFilePath));
    }
}

void AgentDockSessionController::updateSessionFileWatcher()
{
    const QString storagePath = currentProjectStorageFilePath();
    const QString goalPath =
        storagePath.isEmpty() ? QString() : Migration::projectGoalFilePath(storagePath);
    const QString logPath =
        storagePath.isEmpty() ? QString() : Migration::projectActionsLogFilePath(storagePath);
    const QFileInfo storageInfo(storagePath);
    m_sessionStoragePresent =
        !storagePath.isEmpty() &&
        (QFileInfo::exists(storagePath) || QFileInfo::exists(goalPath) ||
         QFileInfo::exists(logPath) || QDir(storageInfo.absolutePath()).exists());

    const QStringList currentPaths =
        m_sessionFileWatcher->files() + m_sessionFileWatcher->directories();
    if (!currentPaths.isEmpty())
    {
        m_sessionFileWatcher->removePaths(currentPaths);
    }

    const QStringList watchPaths = currentSessionWatchPaths();
    if (!watchPaths.isEmpty())
    {
        m_sessionFileWatcher->addPaths(watchPaths);
    }
}

void AgentDockSessionController::reloadSessionFromDisk()
{
    const QString storagePath = currentProjectStorageFilePath();
    if (storagePath.isEmpty())
    {
        return;
    }

    const QString goalPath = Migration::projectGoalFilePath(storagePath);
    const QString logPath = Migration::projectActionsLogFilePath(storagePath);
    const QFileInfo storageInfo(storagePath);
    const bool sessionExists = QFileInfo::exists(storagePath) || QFileInfo::exists(goalPath) ||
                               QFileInfo::exists(logPath) ||
                               QDir(storageInfo.absolutePath()).exists();
    if (!sessionExists && !m_sessionStoragePresent)
    {
        updateSessionFileWatcher();
        return;
    }

    const bool wasRunning = m_dock.m_stopBtn->isEnabled();
    m_dock.clearChatState();
    applyProjectUiDefaults();
    m_projectMcpServers.clear();
    restoreChat();
    updateSessionFileWatcher();
    m_dock.updateRunState(wasRunning);
}

QString AgentDockSessionController::projectDirForPath(const QString &projectFilePath) const
{
    if (projectFilePath.isEmpty())
    {
        return {};
    }

    if (auto *ctx = m_dock.m_controller->editorContext())
    {
        const auto projects = ctx->openProjects();
        for (const auto &project : projects)
        {
            if (project.projectFilePath == projectFilePath)
            {
                return project.projectDir;
            }
        }
    }

    return QFileInfo(projectFilePath).absolutePath();
}

}  // namespace qcai2
