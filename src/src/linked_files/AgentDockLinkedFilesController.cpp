/*! Implements linked-files behavior extracted from AgentDockWidget. */

#include "AgentDockLinkedFilesController.h"

#include "../session/AgentDockSessionController.h"
#include "../ui/AgentDockWidget.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonArray>
#include <QListWidgetItem>
#include <QRegularExpression>

namespace qcai2
{

namespace
{

QSize linkedFileItemSize(const QFontMetrics &metrics, const QString &text, int iconWidth)
{
    return QSize(metrics.horizontalAdvance(text) + iconWidth + 26, qMax(metrics.height(), 16) + 8);
}

bool hasIgnoredLinkedPathPrefix(const QStringList &ignoredPrefixes, const QString &path)
{
    for (const QString &prefix : ignoredPrefixes)
    {
        if (!prefix.isEmpty() && path.startsWith(prefix))
        {
            return true;
        }
    }

    return false;
}

}  // namespace

AgentDockLinkedFilesController::AgentDockLinkedFilesController(AgentDockWidget &dock)
    : m_dock(dock)
{
}

QString AgentDockLinkedFilesController::currentProjectDir() const
{
    if (auto *ctx = m_dock.m_controller->editorContext(); ((ctx != nullptr) == true))
    {
        return ctx->capture().projectDir;
    }
    return {};
}

QString AgentDockLinkedFilesController::normalizeLinkedFilePath(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() == true)
    {
        return {};
    }

    QFileInfo info(trimmed);
    QString absolutePath = info.isAbsolute()
                               ? info.absoluteFilePath()
                               : QFileInfo(linkedFileAbsolutePath(trimmed)).absoluteFilePath();
    if (absolutePath.isEmpty() == true)
    {
        absolutePath = QFileInfo(trimmed).absoluteFilePath();
    }

    const QFileInfo absoluteInfo(absolutePath);
    if (((!absoluteInfo.exists() || !absoluteInfo.isFile()) == true))
    {
        return {};
    }

    const QString projectDir = currentProjectDir();
    if (projectDir.isEmpty() == false)
    {
        const QString relative =
            QDir(projectDir).relativeFilePath(absoluteInfo.absoluteFilePath());
        if (((!relative.startsWith(QStringLiteral("../"))) == true))
        {
            return QDir::cleanPath(relative);
        }
    }

    return QDir::cleanPath(absoluteInfo.absoluteFilePath());
}

QString AgentDockLinkedFilesController::linkedFileAbsolutePath(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() == true)
    {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.isAbsolute() == true)
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    const QString projectDir = currentProjectDir();
    if (projectDir.isEmpty() == true)
    {
        return {};
    }

    return QDir(projectDir).absoluteFilePath(trimmed);
}

QStringList AgentDockLinkedFilesController::linkedFileCandidates() const
{
    const QString projectDir = currentProjectDir();
    if (projectDir.isEmpty() == true)
    {
        return {};
    }

    if (((m_cachedLinkedFileRoot == projectDir && !m_cachedLinkedFileCandidates.isEmpty()) ==
         true))
    {
        return m_cachedLinkedFileCandidates;
    }

    QStringList candidates;
    QDirIterator it(projectDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir rootDir(projectDir);
    while (it.hasNext() == true)
    {
        const QString absolutePath = it.next();
        const QString relativePath = QDir::cleanPath(rootDir.relativeFilePath(absolutePath));
        if (((relativePath.startsWith(QStringLiteral(".qcai2/")) ||
              relativePath.startsWith(QStringLiteral(".git/"))) == true))
        {
            continue;
        }
        if (hasIgnoredLinkedPathPrefix(m_ignoredLinkedFiles, relativePath))
        {
            continue;
        }
        candidates.append(relativePath);
    }

    candidates.sort(Qt::CaseInsensitive);
    m_cachedLinkedFileRoot = projectDir;
    m_cachedLinkedFileCandidates = candidates;
    return m_cachedLinkedFileCandidates;
}

QStringList AgentDockLinkedFilesController::linkedFilesFromGoalText() const
{
    QStringList files;
    static const QRegularExpression re(QStringLiteral(R"((?:^|\s)#([^\s#]+))"));
    QRegularExpressionMatchIterator it = re.globalMatch(m_dock.m_goalEdit->toPlainText());
    while (it.hasNext() == true)
    {
        const QRegularExpressionMatch match = it.next();
        const QString normalized = normalizeLinkedFilePath(match.captured(1));
        if (!normalized.isEmpty() && !files.contains(normalized))
        {
            files.append(normalized);
        }
    }
    return files;
}

QString AgentDockLinkedFilesController::currentEditorLinkedFile() const
{
    if (((m_dock.m_controller == nullptr || m_dock.m_controller->editorContext() == nullptr) ==
         true))
    {
        return {};
    }

    return normalizeLinkedFilePath(m_dock.m_controller->editorContext()->capture().filePath);
}

QString AgentDockLinkedFilesController::defaultLinkedFile() const
{
    const QString normalized = currentEditorLinkedFile();
    if (((normalized.isEmpty() || hasIgnoredLinkedPathPrefix(m_ignoredLinkedFiles, normalized) ||
          m_hiddenDefaultLinkedFiles.contains(normalized)) == true))
    {
        return {};
    }

    return normalized;
}

QStringList AgentDockLinkedFilesController::effectiveLinkedFiles() const
{
    QStringList files = m_manualLinkedFiles;
    const QString defaultFile = defaultLinkedFile();
    if (!defaultFile.isEmpty() && !files.contains(defaultFile))
    {
        files.append(defaultFile);
    }
    for (const QString &path : linkedFilesFromGoalText())
    {
        if (!files.contains(path))
        {
            files.append(path);
        }
    }
    return files;
}

void AgentDockLinkedFilesController::addLinkedFiles(const QStringList &paths)
{
    bool changed = false;
    for (const QString &path : paths)
    {
        const QString normalized = normalizeLinkedFilePath(path);
        if (normalized.isEmpty())
        {
            continue;
        }
        changed = m_hiddenDefaultLinkedFiles.removeAll(normalized) > 0 || changed;
        if (m_manualLinkedFiles.contains(normalized))
        {
            continue;
        }
        m_manualLinkedFiles.append(normalized);
        changed = true;
    }

    if (changed == false)
    {
        return;
    }

    refreshUi();
    m_dock.m_sessionController->saveChat();
}

void AgentDockLinkedFilesController::removeSelectedLinkedFiles()
{
    if (((m_dock.m_linkedFilesView == nullptr) == true))
    {
        return;
    }

    const QList<QListWidgetItem *> selectedItems = m_dock.m_linkedFilesView->selectedItems();
    if (selectedItems.isEmpty())
    {
        return;
    }

    QString goalText = m_dock.m_goalEdit->toPlainText();
    bool goalChanged = false;
    bool manualChanged = false;
    bool hiddenChanged = false;
    const QString currentDefaultFile = currentEditorLinkedFile();

    for (QListWidgetItem *item : selectedItems)
    {
        const QString path = item->data(Qt::UserRole).toString();
        manualChanged = m_manualLinkedFiles.removeAll(path) > 0 || manualChanged;
        if (path == currentDefaultFile && !m_hiddenDefaultLinkedFiles.contains(path))
        {
            m_hiddenDefaultLinkedFiles.append(path);
            hiddenChanged = true;
        }

        const QRegularExpression tokenRe(
            QStringLiteral(R"((^|\s)#%1(?=\s|$))").arg(QRegularExpression::escape(path)));
        const QString updatedGoal = goalText.replace(tokenRe, QStringLiteral("\\1"));
        goalChanged = goalChanged || (updatedGoal != goalText);
        goalText = updatedGoal;
    }

    if (goalChanged == true)
    {
        m_dock.m_goalEdit->setPlainText(goalText);
    }

    if (((manualChanged || goalChanged || hiddenChanged) == true))
    {
        refreshUi();
        m_dock.m_sessionController->saveChat();
    }
}

void AgentDockLinkedFilesController::refreshUi()
{
    if (((m_dock.m_linkedFilesView == nullptr) == true))
    {
        return;
    }

    const QStringList linkedFiles = effectiveLinkedFiles();
    m_dock.m_linkedFilesView->clear();
    m_dock.m_linkedFilesView->setVisible(!linkedFiles.isEmpty());

    if (linkedFiles.isEmpty())
    {
        m_dock.m_linkedFilesView->syncToContents();
        return;
    }

    static QFileIconProvider iconProvider;
    const QFontMetrics metrics(m_dock.m_linkedFilesView->font());
    const int iconWidth = m_dock.m_linkedFilesView->iconSize().width();
    for (const QString &path : linkedFiles)
    {
        const QString absolutePath = linkedFileAbsolutePath(path);
        const QFileInfo fileInfo(absolutePath.isEmpty() ? path : absolutePath);
        const QString label = fileInfo.fileName().isEmpty() ? path : fileInfo.fileName();
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, path);
        item->setToolTip(absolutePath);
        item->setIcon(iconProvider.icon(fileInfo));
        item->setSizeHint(linkedFileItemSize(metrics, label, iconWidth));
        m_dock.m_linkedFilesView->addItem(item);
    }
    m_dock.m_linkedFilesView->syncToContents();
}

QString AgentDockLinkedFilesController::linkedFilesPromptContext() const
{
    const QStringList linkedFiles = effectiveLinkedFiles();
    if (linkedFiles.isEmpty())
    {
        return {};
    }

    QString context = QStringLiteral("Linked files for this request:\n");
    for (const QString &path : linkedFiles)
    {
        context += QStringLiteral("- %1\n").arg(path);
    }

    context += QStringLiteral("\nUse these linked files as explicit request context.\n");

    static constexpr qsizetype kMaxTotalChars = 60000;
    qsizetype usedChars = 0;
    for (const QString &path : linkedFiles)
    {
        const QString absolutePath = linkedFileAbsolutePath(path);
        QFile file(absolutePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }

        QString content = QString::fromUtf8(file.readAll());
        const qsizetype remaining = kMaxTotalChars - usedChars;
        if (remaining <= 0)
        {
            break;
        }
        if (content.size() > remaining)
        {
            content = content.left(remaining) + QStringLiteral("\n... [truncated]");
        }

        context += QStringLiteral("\nFile: %1\n~~~~text\n%2\n~~~~\n").arg(path, content);
        usedChars += content.size();
    }

    return context.trimmed();
}

void AgentDockLinkedFilesController::clearState()
{
    m_manualLinkedFiles.clear();
    m_ignoredLinkedFiles.clear();
    m_hiddenDefaultLinkedFiles.clear();
    invalidateCandidates();
}

const QStringList &AgentDockLinkedFilesController::manualLinkedFiles() const
{
    return m_manualLinkedFiles;
}

const QStringList &AgentDockLinkedFilesController::ignoredLinkedFiles() const
{
    return m_ignoredLinkedFiles;
}

void AgentDockLinkedFilesController::setManualLinkedFiles(const QStringList &paths)
{
    m_manualLinkedFiles = paths;
}

void AgentDockLinkedFilesController::setIgnoredLinkedFiles(const QStringList &paths)
{
    m_ignoredLinkedFiles = paths;
}

void AgentDockLinkedFilesController::invalidateCandidates()
{
    m_cachedLinkedFileRoot.clear();
    m_cachedLinkedFileCandidates.clear();
}

}  // namespace qcai2
