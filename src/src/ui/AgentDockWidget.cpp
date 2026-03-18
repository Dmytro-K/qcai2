/*! @file
    @brief Implements the dock widget UI for chat, diff review, and approval handling.
*/

#include "AgentDockWidget.h"
#include "../goal/FileReferenceGoalHandler.h"
#include "../goal/SlashCommandGoalHandler.h"
#include "../linked_files/AgentDockLinkedFilesController.h"
#include "../session/AgentDockSessionController.h"
#include "../settings/Settings.h"
#include "../util/Diff.h"
#include "../util/Logger.h"
#include "../util/Migration.h"
#include "ui_AgentDockWidget.h"

#include <coreplugin/editormanager/editormanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextOption>
#include <QVBoxLayout>

#include <functional>
#include <limits>

namespace qcai2
{

namespace
{

int boundedInt(qsizetype value)
{
    return static_cast<int>(std::min(value, qsizetype(std::numeric_limits<int>::max())));
}

QStringList openAiAgentModels()
{
    return {
        QStringLiteral("gpt-5.4"),           QStringLiteral("gpt-5.2"),
        QStringLiteral("gpt-5.3-codex"),     QStringLiteral("gpt-5-mini"),
        QStringLiteral("gpt-4.1"),           QStringLiteral("o3"),
        QStringLiteral("o4-mini"),           QStringLiteral("claude-opus-4.6"),
        QStringLiteral("claude-sonnet-4.6"), QStringLiteral("gemini-3-flash"),
    };
}

void populateEffortCombo(QComboBox *combo)
{
    combo->addItem(QObject::tr("Off"), QStringLiteral("off"));
    combo->addItem(QObject::tr("Low"), QStringLiteral("low"));
    combo->addItem(QObject::tr("Medium"), QStringLiteral("medium"));
    combo->addItem(QObject::tr("High"), QStringLiteral("high"));
}

void selectEffortValue(QComboBox *combo, const QString &value, int fallbackIndex)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallbackIndex);
}

void populateModeCombo(QComboBox *combo)
{
    combo->addItem(QObject::tr("Ask"), QStringLiteral("ask"));
    combo->addItem(QObject::tr("Agent"), QStringLiteral("agent"));
}

void repopulateEditableCombo(QComboBox *combo, const QStringList &items,
                             const QString &selectedText)
{
    QStringList uniqueItems;
    uniqueItems.reserve(items.size() + 1);

    for (const QString &item : items)
    {
        const QString trimmed = item.trimmed();
        if (trimmed.isEmpty() || uniqueItems.contains(trimmed))
        {
            continue;
        }
        uniqueItems.append(trimmed);
    }

    const QString selected = selectedText.trimmed();
    if (!selected.isEmpty() && !uniqueItems.contains(selected))
    {
        uniqueItems.append(selected);
    }

    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItems(uniqueItems);
    combo->setCurrentText(selected);
}

QString wrapMarkdownPayloadBlocks(const QString &markdown)
{
    auto applyPattern = [](const QString &text, const QRegularExpression &pattern) {
        QString rewritten;
        qsizetype cursor = 0;
        QRegularExpressionMatchIterator it = pattern.globalMatch(text);
        while (it.hasNext() == true)
        {
            const QRegularExpressionMatch match = it.next();
            rewritten += text.mid(cursor, match.capturedStart() - cursor);
            rewritten += match.captured(1);
            rewritten += QStringLiteral("\n\n~~~~text\n");
            rewritten += match.captured(2);
            if (((match.captured(2).endsWith(QLatin1Char('\n'))) == false))
            {
                rewritten += QLatin1Char('\n');
            }
            rewritten += QStringLiteral("~~~~");
            rewritten += match.captured(3);
            cursor = match.capturedEnd();
        }
        rewritten += text.mid(cursor);
        return rewritten;
    };

    QString rewritten = markdown;
    static const QRegularExpression toolReturnedRe(QStringLiteral(
        R"((Tool '[^']+' returned:)\n([\s\S]*?)(\n\nContinue with the next step\.))"));
    static const QRegularExpression approvedResultRe(
        QStringLiteral(R"((Approved and executed '[^']+'. Result:)\n([\s\S]*?)(\n\nContinue\.))"));

    rewritten = applyPattern(rewritten, toolReturnedRe);
    rewritten = applyPattern(rewritten, approvedResultRe);
    return rewritten;
}

QString redactReadFilePayloads(const QString &markdown)
{
    QString rewritten;
    qsizetype cursor = 0;
    static const QRegularExpression readFileReturnedRe(QStringLiteral(
        R"(((?:Human:\s*)?Tool 'read_file' returned:)([\s\S]*?)(?=\n\nContinue with the next step\.|\n(?:Human:|Assistant:|\{"type":)|$))"));

    QRegularExpressionMatchIterator it = readFileReturnedRe.globalMatch(markdown);
    while (it.hasNext() == true)
    {
        const QRegularExpressionMatch match = it.next();
        rewritten += markdown.mid(cursor, match.capturedStart() - cursor);
        rewritten += match.captured(1);
        rewritten += QStringLiteral("\n\n*[read_file contents omitted from log]*");
        cursor = match.capturedEnd();
    }

    rewritten += markdown.mid(cursor);
    return rewritten;
}

QString providerUsageMarkdown(const ProviderUsage &usage, qint64 durationMs)
{
    const QString summary = formatProviderUsageSummary(usage, durationMs);
    if (summary.isEmpty() == true)
    {
        return {};
    }

    return QStringLiteral("<span style=\"color:#7aa2f7;\"><strong>Usage:</strong> %1</span>")
        .arg(summary.toHtmlEscaped());
}

class DiffHighlighter final : public QSyntaxHighlighter
{
public:
    explicit DiffHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
    {
        const QPalette palette = QGuiApplication::palette();
        const bool isDark = palette.color(QPalette::Base).lightness() < 128;
        const QColor textColor = palette.color(QPalette::Text);

        m_addedFormat.setForeground(textColor);
        m_addedFormat.setBackground(isDark ? QColor(QStringLiteral("#1f3a2a"))
                                           : QColor(QStringLiteral("#e8f5e9")));
        m_removedFormat.setForeground(textColor);
        m_removedFormat.setBackground(isDark ? QColor(QStringLiteral("#3b2424"))
                                             : QColor(QStringLiteral("#fdeaea")));
        m_hunkFormat.setForeground(isDark ? QColor(QStringLiteral("#9ecbff"))
                                          : QColor(QStringLiteral("#0b4f9e")));
        m_hunkFormat.setBackground(isDark ? QColor(QStringLiteral("#1d2f45"))
                                          : QColor(QStringLiteral("#e7f1ff")));
        m_hunkFormat.setFontWeight(QFont::Bold);
        m_fileHeaderFormat.setForeground(textColor);
        m_fileHeaderFormat.setBackground(isDark ? QColor(QStringLiteral("#2f2f2f"))
                                                : QColor(QStringLiteral("#ececec")));
        m_metaFormat.setForeground(isDark ? QColor(QStringLiteral("#b7beca"))
                                          : QColor(QStringLiteral("#566070")));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (((text.startsWith(QStringLiteral("@@"))) == true))
        {
            return setFormat(0, boundedInt(text.size()), m_hunkFormat);
        }
        if (((text.startsWith(QStringLiteral("+++ ")) ||
              text.startsWith(QStringLiteral("--- "))) == true))
        {
            return setFormat(0, boundedInt(text.size()), m_fileHeaderFormat);
        }
        if (((text.startsWith(QStringLiteral("diff --")) ||
              text.startsWith(QStringLiteral("index ")) ||
              text.startsWith(QStringLiteral("new file")) ||
              text.startsWith(QStringLiteral("deleted file"))) == true))
        {
            return setFormat(0, boundedInt(text.size()), m_metaFormat);
        }
        if (((text.startsWith(QLatin1Char('+'))) == true))
        {
            return setFormat(0, boundedInt(text.size()), m_addedFormat);
        }
        if (((text.startsWith(QLatin1Char('-'))) == true))
        {
            return setFormat(0, boundedInt(text.size()), m_removedFormat);
        }
    }

private:
    QTextCharFormat m_addedFormat;
    QTextCharFormat m_removedFormat;
    QTextCharFormat m_hunkFormat;
    QTextCharFormat m_fileHeaderFormat;
    QTextCharFormat m_metaFormat;
};

class DiffPreviewEdit;

class DiffLineNumberArea final : public QWidget
{
public:
    explicit DiffLineNumberArea(DiffPreviewEdit *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    DiffPreviewEdit *m_editor;
};

class DiffPreviewEdit final : public QPlainTextEdit
{
public:
    explicit DiffPreviewEdit(QWidget *parent = nullptr)
        : QPlainTextEdit(parent), m_lineNumberArea(new DiffLineNumberArea(this)),
          m_highlighter(document())
    {
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                &DiffPreviewEdit::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest, this,
                &DiffPreviewEdit::updateLineNumberArea);
        updateLineNumberAreaWidth(0);
    }

    bool allChangesApproved() const
    {
        return m_approvableLines.isEmpty() || m_approvedLines.size() == m_approvableLines.size();
    }

    bool hasApprovedChanges() const
    {
        return !m_approvedLines.isEmpty();
    }

    void setDiffText(const QString &diff)
    {
        setPlainText(diff);
        recalculateApprovals();
        notifyApprovalChanged();
        m_lineNumberArea->update();
    }

    void setApprovalChangedCallback(std::function<void(qsizetype, qsizetype)> callback)
    {
        m_approvalChangedCallback = std::move(callback);
        notifyApprovalChanged();
    }

    QString approvedDiff() const
    {
        const QStringList lines = toPlainText().split('\n');
        if (lines.isEmpty())
        {
            return QString();
        }

        auto isFileStart = [&lines](int index) {
            const QString &line = lines[index];
            const QString next = index + 1 < lines.size() ? lines[index + 1] : QString();
            return line.startsWith(QStringLiteral("diff --git ")) ||
                   line.startsWith(QStringLiteral("Index: ")) ||
                   (line.startsWith(QStringLiteral("--- ")) &&
                    next.startsWith(QStringLiteral("+++ ")));
        };

        QStringList outputLines;
        QStringList fileHeaderLines;
        QStringList keptHunkLines;
        bool inFileSection = false;

        auto flushFileSection = [&]() {
            if (((!keptHunkLines.isEmpty()) == true))
            {
                outputLines.append(fileHeaderLines);
                outputLines.append(keptHunkLines);
            }
            fileHeaderLines.clear();
            keptHunkLines.clear();
            inFileSection = false;
        };

        int i = 0;
        while (i < lines.size())
        {
            if (isFileStart(i))
            {
                if (inFileSection == true)
                {
                    flushFileSection();
                }
                inFileSection = true;
                fileHeaderLines.append(lines[i]);
                ++i;
                continue;
            }

            if (inFileSection == false)
            {
                outputLines.append(lines[i]);
                ++i;
                continue;
            }

            if (((!lines[i].startsWith(QStringLiteral("@@ "))) == true))
            {
                fileHeaderLines.append(lines[i]);
                ++i;
                continue;
            }

            QStringList candidateHunk;
            bool hunkHasApprovedChanges = false;
            candidateHunk.append(lines[i]);
            ++i;

            while (i < lines.size() && !lines[i].startsWith(QStringLiteral("@@ ")) &&
                   !isFileStart(i))
            {
                const QString &hunkLine = lines[i];
                const int lineNumber = i + 1;

                if (((hunkLine.startsWith(QLatin1Char('+')) &&
                      !hunkLine.startsWith(QStringLiteral("+++ "))) == true))
                {
                    if (m_approvedLines.contains(lineNumber) == true)
                    {
                        candidateHunk.append(hunkLine);
                        hunkHasApprovedChanges = true;
                    }
                }
                else if (((hunkLine.startsWith(QLatin1Char('-')) &&
                           !hunkLine.startsWith(QStringLiteral("--- "))) == true))
                {
                    if (m_approvedLines.contains(lineNumber) == true)
                    {
                        candidateHunk.append(hunkLine);
                        hunkHasApprovedChanges = true;
                    }
                    else
                    {
                        candidateHunk.append(QStringLiteral(" ") + hunkLine.mid(1));
                    }
                }
                else
                {
                    candidateHunk.append(hunkLine);
                }
                ++i;
            }

            if (hunkHasApprovedChanges == true)
            {
                keptHunkLines.append(candidateHunk);
            }
        }

        if (inFileSection == true)
        {
            flushFileSection();
        }

        return Diff::normalize(outputLines.join('\n'));
    }

    void lineNumberAreaMousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() == true)
        {
            if (((block.isVisible() && event->pos().y() >= top && event->pos().y() < bottom) ==
                 true))
            {
                toggleApproval(blockNumber + 1);
                break;
            }
            if (((top > event->pos().y()) == true))
            {
                break;
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (((max >= 10) == true))
        {
            max /= 10;
            ++digits;
        }
        return 24 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), palette().alternateBase());
        painter.setPen(palette().color(QPalette::Mid));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (((block.isValid() && top <= event->rect().bottom()) == true))
        {
            if (((block.isVisible() && bottom >= event->rect().top()) == true))
            {
                const int lineNumber = blockNumber + 1;
                if (m_approvableLines.contains(lineNumber) == true)
                {
                    const QRect markerRect(2, top + (fontMetrics().height() - 9) / 2, 9, 9);
                    painter.setPen(palette().color(QPalette::Mid));
                    painter.setBrush(m_approvedLines.contains(lineNumber)
                                         ? QBrush(QColor(QStringLiteral("#4caf50")))
                                         : Qt::NoBrush);
                    painter.drawRect(markerRect);
                }

                const QString number = QString::number(blockNumber + 1);
                painter.drawText(14, top, m_lineNumberArea->width() - 18, fontMetrics().height(),
                                 Qt::AlignRight | Qt::AlignVCenter, number);
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(
            QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

private:
    void updateLineNumberAreaWidth(int)
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }
    void updateLineNumberArea(const QRect &rect, int dy)
    {
        if (((dy != 0) == true))
        {
            m_lineNumberArea->scroll(0, dy);
        }
        else
        {
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        }
        if (rect.contains(viewport()->rect()))
        {
            updateLineNumberAreaWidth(0);
        }
    }

    void recalculateApprovals()
    {
        m_approvableLines.clear();
        m_approvedLines.clear();

        for (QTextBlock block = document()->firstBlock(); ((block.isValid()) == true);
             block = block.next())
        {
            const QString text = block.text();
            if ((((text.startsWith(QLatin1Char('+')) &&
                   !text.startsWith(QStringLiteral("+++ "))) ||
                  (text.startsWith(QLatin1Char('-')) &&
                   !text.startsWith(QStringLiteral("--- ")))) == true))
            {
                m_approvableLines.insert(block.blockNumber() + 1);
            }
        }
    }

    void toggleApproval(int line)
    {
        if (((!m_approvableLines.contains(line)) == true))
        {
            return;
        }

        if (m_approvedLines.contains(line) == true)
        {
            m_approvedLines.remove(line);
        }
        else
        {
            m_approvedLines.insert(line);
        }

        notifyApprovalChanged();
        m_lineNumberArea->update();
    }

    void notifyApprovalChanged()
    {
        if (static_cast<bool>(m_approvalChangedCallback) == true)
        {
            m_approvalChangedCallback(m_approvedLines.size(), m_approvableLines.size());
        }
    }

    DiffLineNumberArea *m_lineNumberArea;
    DiffHighlighter m_highlighter;
    QSet<int> m_approvableLines;
    QSet<int> m_approvedLines;
    std::function<void(qsizetype, qsizetype)> m_approvalChangedCallback;
};

DiffLineNumberArea::DiffLineNumberArea(DiffPreviewEdit *editor) : QWidget(editor), m_editor(editor)
{
}
QSize DiffLineNumberArea::sizeHint() const
{
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}
void DiffLineNumberArea::paintEvent(QPaintEvent *event)
{
    m_editor->lineNumberAreaPaintEvent(event);
}
void DiffLineNumberArea::mousePressEvent(QMouseEvent *event)
{
    m_editor->lineNumberAreaMousePressEvent(event);
}

}  // namespace

AgentDockWidget::AgentDockWidget(AgentController *controller,
                                 ChatContextManager *chatContextManager, QWidget *parent)
    : QWidget(parent), m_controller(controller),
      m_linkedFilesController(std::make_unique<AgentDockLinkedFilesController>(*this)),
      m_sessionController(std::make_unique<AgentDockSessionController>(*this, chatContextManager))
{
    setupUi();

    m_inlineDiffManager = new InlineDiffManager(this);

    // Throttle rendering during streaming to avoid O(n²) setPlainText() calls
    m_renderThrottle = new QTimer(this);
    m_renderThrottle->setSingleShot(true);
    m_renderThrottle->setInterval(50);
    connect(m_renderThrottle, &QTimer::timeout, this, &AgentDockWidget::renderLog);

    // Connect controller signals
    connect(m_controller, &AgentController::logMessage, this, &AgentDockWidget::onLogMessage);
    connect(m_controller, &AgentController::providerUsageAvailable, this,
            &AgentDockWidget::onProviderUsageAvailable);
    connect(m_controller, &AgentController::statusChanged, this,
            [this](const QString &status) { m_statusLabel->setText(status); });
    connect(m_controller, &AgentController::streamingToken, this, [this](const QString &token) {
        m_streamingMarkdown += token;
        if (!m_isStreaming)
        {
            m_isStreaming = true;
            m_streamingRenderedLen = 0;
        }
        if (!m_renderThrottle->isActive())
        {
            m_renderThrottle->start();
        }
    });
    connect(m_controller, &AgentController::planUpdated, this, &AgentDockWidget::onPlanUpdated);
    connect(m_controller, &AgentController::diffAvailable, this,
            &AgentDockWidget::onDiffAvailable);
    connect(m_controller, &AgentController::approvalRequested, this,
            &AgentDockWidget::onApprovalRequested);
    connect(m_controller, &AgentController::iterationChanged, this,
            &AgentDockWidget::onIterationChanged);
    connect(m_controller, &AgentController::stopped, this, &AgentDockWidget::onStopped);
    connect(m_inlineDiffManager, &InlineDiffManager::diffChanged, this,
            [this](const QString &diff) {
                syncDiffUi(diff, false, false);
                m_sessionController->saveChat();
            });
    connect(m_inlineDiffManager, &InlineDiffManager::hunkAccepted, this,
            [this](int, const QString &filePath) {
                onLogMessage(QStringLiteral("✅ Inline diff accepted: %1").arg(filePath));
            });
    connect(m_inlineDiffManager, &InlineDiffManager::hunkRejected, this,
            [this](int, const QString &filePath) {
                onLogMessage(QStringLiteral("❌ Inline diff rejected: %1").arg(filePath));
            });
    connect(m_inlineDiffManager, &InlineDiffManager::allResolved, this,
            [this]() { onLogMessage(QStringLiteral("✅ All inline diff hunks resolved.")); });
    connect(m_controller, &AgentController::errorOccurred, this, [this](const QString &err) {
        onLogMessage(QStringLiteral("❌ Error: %1").arg(err));
        updateRunState(false);
    });

    // All hotkeys are handled via event filter on goal edit only
    m_goalEdit->installEventFilter(this);

    // Connect debug logger to Debug Log tab
    connect(&Logger::instance(), &Logger::entryAdded, this,
            [this](const QString &entry) { m_debugLogView->appendPlainText(entry); });
    // Load existing log entries
    const auto existingEntries = Logger::instance().entries();
    for (const auto &e : existingEntries)
    {
        m_debugLogView->appendPlainText(e);
    }

    if (auto *projectManager = ProjectExplorer::ProjectManager::instance())
    {
        connect(
            projectManager, &ProjectExplorer::ProjectManager::projectAdded, this,
            [this](ProjectExplorer::Project *) { m_sessionController->refreshProjectSelector(); });
        connect(
            projectManager, &ProjectExplorer::ProjectManager::projectRemoved, this,
            [this](ProjectExplorer::Project *) { m_sessionController->refreshProjectSelector(); });
        connect(
            projectManager, &ProjectExplorer::ProjectManager::projectDisplayNameChanged, this,
            [this](ProjectExplorer::Project *) { m_sessionController->refreshProjectSelector(); });
        connect(
            projectManager, &ProjectExplorer::ProjectManager::startupProjectChanged, this,
            [this](ProjectExplorer::Project *) { m_sessionController->refreshProjectSelector(); });
    }

    if (auto *editorManager = Core::EditorManager::instance())
    {
        connect(editorManager, &Core::EditorManager::currentEditorChanged, this,
                [this](Core::IEditor *) { m_linkedFilesController->refreshUi(); });
    }

    m_sessionController->refreshProjectSelector();
}

AgentDockWidget::~AgentDockWidget()
{
    m_sessionController->saveChat();
}

void AgentDockWidget::focusGoalInput()
{
    if (m_goalEdit == nullptr)
    {
        return;
    }

    m_goalEdit->setFocus(Qt::ShortcutFocusReason);
    QTextCursor cursor = m_goalEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_goalEdit->setTextCursor(cursor);
}

QString AgentDockWidget::currentLogMarkdown() const
{
    QString text = m_logMarkdown;
    if (!m_streamingMarkdown.isEmpty())
    {
        if (!text.isEmpty())
        {
            text += QStringLiteral("\n\n");
        }
        text += m_streamingMarkdown;
    }
    return redactReadFilePayloads(text);
}

void AgentDockWidget::syncDiffUi(const QString &diff, bool focusDiffTab, bool refreshInlineMarkers)
{
    m_currentDiff = diff;
    m_appliedDiff.clear();
    m_revertPatchBtn->setEnabled(false);

    auto *diffPreview = static_cast<DiffPreviewEdit *>(m_diffView);
    diffPreview->setDiffText(diff);
    m_applyPatchBtn->setEnabled(!diff.isEmpty() && diffPreview->hasApprovedChanges());

    if (focusDiffTab)
    {
        if (auto *page = m_diffView->parentWidget())
        {
            m_tabs->setCurrentWidget(page);
        }
    }

    m_diffFileList->clear();
    const QString workDir = m_sessionController->currentProjectDir();

    if (!diff.isEmpty())
    {
        static const QRegularExpression reFile(QStringLiteral(R"(^--- a/(.+)$)"),
                                               QRegularExpression::MultilineOption);
        auto it = reFile.globalMatch(diff);
        QStringList files;
        while (it.hasNext())
        {
            const QString f = it.next().captured(1);
            if (!files.contains(f))
            {
                files.append(f);
            }
        }

        for (const auto &f : files)
        {
            auto *item = new QListWidgetItem(f);
            item->setData(Qt::UserRole, QDir(workDir).absoluteFilePath(f));
            m_diffFileList->addItem(item);
        }

        disconnect(m_diffFileList, &QListWidget::itemClicked, nullptr, nullptr);
        connect(m_diffFileList, &QListWidget::itemClicked, this, [](QListWidgetItem *item) {
            const QString absPath = item->data(Qt::UserRole).toString();
            Core::EditorManager::openEditorAt(
                Utils::Link(Utils::FilePath::fromString(absPath), 0, 0));
        });
    }

    if (refreshInlineMarkers)
    {
        if (!diff.isEmpty() && !workDir.isEmpty())
        {
            m_inlineDiffManager->showDiff(diff, workDir);
        }
        else
        {
            m_inlineDiffManager->clearAll();
        }
    }

    m_diffFileList->setVisible(m_diffFileList->count() != 0);
}

void AgentDockWidget::clearChatState()
{
    m_goalEdit->clear();
    m_linkedFilesController->clearState();
    m_logView->clear();
    m_rawMarkdownView->clear();
    m_logMarkdown.clear();
    m_streamingMarkdown.clear();
    m_streamingRenderedLen = 0;
    m_isStreaming = false;
    m_planList->clear();
    m_inlineDiffManager->clearAll();
    static_cast<DiffPreviewEdit *>(m_diffView)->setDiffText(QString());
    m_diffFileList->clear();
    m_approvalList->clear();
    m_approvalItems.clear();
    m_appliedDiff.clear();
    m_currentDiff.clear();
    m_statusLabel->setText(tr("Ready"));
    m_applyPatchBtn->setEnabled(false);
    m_revertPatchBtn->setEnabled(false);
    m_tabs->setCurrentWidget(m_planList);
    m_linkedFilesController->refreshUi();
}

void AgentDockWidget::setupUi()
{
    m_ui = std::make_unique<Ui::AgentDockWidget>();
    m_ui->setupUi(this);

    m_statusLabel = m_ui->statusLabel;
    m_projectCombo = m_ui->projectCombo;
    auto *designerGoalEdit = m_ui->goalEdit;
    m_modeCombo = m_ui->modeCombo;
    m_modelCombo = m_ui->modelCombo;
    m_reasoningCombo = m_ui->reasoningCombo;
    m_thinkingCombo = m_ui->thinkingCombo;
    m_runBtn = m_ui->runBtn;
    m_stopBtn = m_ui->stopBtn;
    m_dryRunCheck = m_ui->dryRunCheck;
    m_tabs = m_ui->tabs;
    m_planList = m_ui->planList;
    m_logView = m_ui->logView;
    m_rawMarkdownView = m_ui->rawMarkdownView;
    m_diffFileList = m_ui->diffFileList;
    m_approvalList = m_ui->approvalList;
    m_debugLogView = m_ui->debugLogView;
    m_applyPatchBtn = m_ui->applyPatchBtn;
    m_revertPatchBtn = m_ui->revertPatchBtn;
    m_copyPlanBtn = m_ui->copyPlanBtn;
    auto *newChatBtn = m_ui->newChatButton;

    m_ui->contentSplitter->setStretchFactor(0, 3);
    m_ui->contentSplitter->setStretchFactor(1, 1);
    m_ui->inputRow->setStretch(0, 1);
    m_ui->modeModelRow->setStretch(2, 1);

    m_linkedFilesView = new LinkedFilesListWidget(this);
    m_linkedFilesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_linkedFilesView->setVisible(false);
    m_linkedFilesView->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto *removeLinkedFileAction = new QAction(tr("Remove Linked File"), m_linkedFilesView);
    connect(removeLinkedFileAction, &QAction::triggered, this,
            [this]() { m_linkedFilesController->removeSelectedLinkedFiles(); });
    m_linkedFilesView->addAction(removeLinkedFileAction);
    m_ui->goalColumn->insertWidget(0, m_linkedFilesView);

    auto *goalEdit = new GoalTextEdit(designerGoalEdit->parentWidget());
    goalEdit->setObjectName(designerGoalEdit->objectName());
    goalEdit->setSizePolicy(designerGoalEdit->sizePolicy());
    goalEdit->setMinimumSize(designerGoalEdit->minimumSize());
    goalEdit->setAcceptRichText(designerGoalEdit->acceptRichText());
    goalEdit->setPlaceholderText(designerGoalEdit->placeholderText());
    goalEdit->setFont(designerGoalEdit->font());
    const int goalEditIndex = m_ui->goalColumn->indexOf(designerGoalEdit);
    m_ui->goalColumn->removeWidget(designerGoalEdit);
    m_ui->goalColumn->insertWidget(goalEditIndex, goalEdit);
    designerGoalEdit->deleteLater();
    m_goalEdit = goalEdit;
    m_goalEdit->addSpecialHandler(std::make_unique<SlashCommandGoalHandler>(&m_slashCommands));
    m_goalEdit->addSpecialHandler(std::make_unique<FileReferenceGoalHandler>(
        [this]() { return m_linkedFilesController->linkedFileCandidates(); }));
    connect(m_goalEdit, &QTextEdit::textChanged, this,
            [this]() { m_linkedFilesController->refreshUi(); });
    connect(m_goalEdit, &GoalTextEdit::filesDropped, this,
            [this](const QStringList &paths) { m_linkedFilesController->addLinkedFiles(paths); });
    connect(m_linkedFilesView, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                const QString absolutePath = m_linkedFilesController->linkedFileAbsolutePath(
                    item->data(Qt::UserRole).toString());
                if (!absolutePath.isEmpty())
                {
                    Core::EditorManager::openEditorAt(
                        Utils::Link(Utils::FilePath::fromString(absolutePath), 0, 0));
                }
            });
    m_linkedFilesView->installEventFilter(this);
    m_linkedFilesController->refreshUi();

    auto *diffPreview = new DiffPreviewEdit(m_ui->diffViewPlaceholder->parentWidget());
    diffPreview->setObjectName(QStringLiteral("diffView"));
    diffPreview->setReadOnly(true);
    diffPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    diffPreview->setFont(QFont(QStringLiteral("monospace")));
    if (auto *layout = m_ui->diffViewPlaceholder->parentWidget()->layout())
    {
        layout->replaceWidget(m_ui->diffViewPlaceholder, diffPreview);
    }
    m_ui->diffViewPlaceholder->deleteLater();
    m_diffView = diffPreview;

    m_logView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_rawMarkdownView->setFont(QFont(QStringLiteral("monospace"), 9));
    m_debugLogView->setFont(QFont(QStringLiteral("monospace"), 9));

    populateModeCombo(m_modeCombo);

    m_modelCombo->setEditable(true);
    const bool useCopilotModels = settings().provider == QStringLiteral("copilot");
    repopulateEditableCombo(
        m_modelCombo, useCopilotModels ? modelCatalog().copilotModels() : openAiAgentModels(),
        settings().modelName);
    connect(&modelCatalog(), &ModelCatalog::copilotModelsChanged, this,
            [this](const QStringList &models) {
                if (settings().provider != QStringLiteral("copilot"))
                {
                    return;
                }
                repopulateEditableCombo(m_modelCombo, models, m_modelCombo->currentText());
            });

    populateEffortCombo(m_reasoningCombo);
    selectEffortValue(m_reasoningCombo, settings().reasoningEffort, 2);
    populateEffortCombo(m_thinkingCombo);
    selectEffortValue(m_thinkingCombo, settings().thinkingLevel, 2);
    m_dryRunCheck->setChecked(settings().dryRunDefault);

    connect(m_applyPatchBtn, &QPushButton::clicked, this, &AgentDockWidget::onApplyPatchClicked);
    connect(m_revertPatchBtn, &QPushButton::clicked, this, &AgentDockWidget::onRevertPatchClicked);
    connect(newChatBtn, &QPushButton::clicked, this, [this]() {
        const bool hasContent = !m_goalEdit->toPlainText().trimmed().isEmpty() ||
                                !m_logView->toPlainText().isEmpty() || m_planList->count() > 0 ||
                                !m_currentDiff.isEmpty() || m_approvalList->count() > 0;

        if (hasContent &&
            QMessageBox::question(
                this, tr("New Chat"), tr("Start a new chat and clear the current history?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            return;
        }

        if (m_stopBtn->isEnabled())
        {
            onStopClicked();
        }

        m_sessionController->startNewConversation();
        clearChatState();
        m_sessionController->saveChat();
    });
    connect(m_copyPlanBtn, &QPushButton::clicked, this, &AgentDockWidget::onCopyPlanClicked);

    static_cast<DiffPreviewEdit *>(m_diffView)
        ->setApprovalChangedCallback([this](qsizetype approved, qsizetype total) {
            Q_UNUSED(total);
            m_applyPatchBtn->setEnabled(!m_currentDiff.isEmpty() && approved > 0);
        });

    connect(m_runBtn, &QPushButton::clicked, this, &AgentDockWidget::onRunClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &AgentDockWidget::onStopClicked);
    connect(m_projectCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
        {
            return;
        }
        m_sessionController->switchProjectContext(m_projectCombo->itemData(index).toString());
    });
    const auto persistProjectUiState = [this]() {
        if (!m_sessionController->currentProjectFilePath().isEmpty())
        {
            m_sessionController->saveChat();
        }
    };
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this,
            [persistProjectUiState](int) { persistProjectUiState(); });
    connect(m_modelCombo, &QComboBox::currentTextChanged, this,
            [persistProjectUiState](const QString &) { persistProjectUiState(); });
    connect(m_reasoningCombo, &QComboBox::currentIndexChanged, this,
            [persistProjectUiState](int) { persistProjectUiState(); });
    connect(m_thinkingCombo, &QComboBox::currentIndexChanged, this,
            [persistProjectUiState](int) { persistProjectUiState(); });
    connect(m_dryRunCheck, &QCheckBox::checkStateChanged, this,
            [persistProjectUiState](Qt::CheckState) { persistProjectUiState(); });

    m_sessionController->applyProjectUiDefaults();

    // Add Ctrl+C/A shortcuts directly on text views (event filter not enough — Qt Creator
    // intercepts)
    auto addCopyShortcut = [](QPlainTextEdit *edit) {
        auto *copyShortcut = new QShortcut(QKeySequence::Copy, edit);
        copyShortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(copyShortcut, &QShortcut::activated, edit, &QPlainTextEdit::copy);
        auto *selectAllShortcut = new QShortcut(QKeySequence::SelectAll, edit);
        selectAllShortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(selectAllShortcut, &QShortcut::activated, edit,
                         &QPlainTextEdit::selectAll);
    };
    auto addCopyShortcutRich = [](QTextEdit *edit) {
        auto *copyShortcut = new QShortcut(QKeySequence::Copy, edit);
        copyShortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(copyShortcut, &QShortcut::activated, edit, &QTextEdit::copy);
        auto *selectAllShortcut = new QShortcut(QKeySequence::SelectAll, edit);
        selectAllShortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(selectAllShortcut, &QShortcut::activated, edit, &QTextEdit::selectAll);
    };
    addCopyShortcutRich(m_logView);
    addCopyShortcut(m_rawMarkdownView);
    addCopyShortcut(m_diffView);
    addCopyShortcut(m_debugLogView);
    auto *goalCopyShortcut = new QShortcut(QKeySequence::Copy, m_goalEdit);
    goalCopyShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(goalCopyShortcut, &QShortcut::activated, m_goalEdit, &QTextEdit::copy);
    auto *goalSelectAllShortcut = new QShortcut(QKeySequence::SelectAll, m_goalEdit);
    goalSelectAllShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(goalSelectAllShortcut, &QShortcut::activated, m_goalEdit,
                     &QTextEdit::selectAll);
}

void AgentDockWidget::updateRunState(bool running)
{
    m_runBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_goalEdit->setReadOnly(running);
    m_modeCombo->setEnabled(!running);
    m_modelCombo->setEnabled(!running);
    m_reasoningCombo->setEnabled(!running);
    m_thinkingCombo->setEnabled(!running);
    m_dryRunCheck->setEnabled(!running);
    m_projectCombo->setEnabled(!running && m_projectCombo->count() > 0 &&
                               !m_projectCombo->itemData(0).toString().isEmpty());
}

bool AgentDockWidget::tryExecuteSlashCommand(QString &goal)
{
    const SlashCommandDispatchResult result = m_slashCommands.dispatch(
        goal, SlashCommandContext{[this](const QString &message) { onLogMessage(message); }});
    if (!result.isSlashCommand)
    {
        return false;
    }

    // Redirect to AI agent when the handler produced a goalOverride.
    if (!result.goalOverride.isEmpty())
    {
        goal = result.goalOverride;
        return false;
    }

    m_tabs->setCurrentWidget(m_logView);

    if (!result.errorMessage.isEmpty())
    {
        onLogMessage(result.errorMessage);
    }

    if (result.executed)
    {
        m_goalEdit->clear();
        m_statusLabel->setText(
            QStringLiteral("Slash command executed: /%1").arg(result.commandName));
    }

    m_sessionController->saveChat();
    return true;
}

void AgentDockWidget::onRunClicked()
{
    QString goal = m_goalEdit->toPlainText().trimmed();
    if (goal.isEmpty())
    {
        return;
    }

    if (tryExecuteSlashCommand(goal))
    {
        return;
    }

    const QString selectedModel = m_modelCombo->currentText().trimmed();
    const QString selectedMode = m_modeCombo->currentData().toString();
    m_controller->setRequestContext(m_linkedFilesController->linkedFilesPromptContext(),
                                    m_linkedFilesController->effectiveLinkedFiles());
    m_goalEdit->clear();

    updateRunState(true);
    m_tabs->setCurrentWidget(m_logView);
    m_controller->start(goal, m_dryRunCheck->isChecked(),
                        selectedMode == QStringLiteral("ask") ? AgentController::RunMode::Ask
                                                              : AgentController::RunMode::Agent,
                        selectedModel, m_reasoningCombo->currentData().toString(),
                        m_thinkingCombo->currentData().toString());
}

void AgentDockWidget::onStopClicked()
{
    m_controller->stop();
    updateRunState(false);
}

void AgentDockWidget::onApplyPatchClicked()
{
    if (m_currentDiff.isEmpty())
    {
        return;
    }

    auto *diffPreview = static_cast<DiffPreviewEdit *>(m_diffView);
    if (!diffPreview->hasApprovedChanges())
    {
        QMessageBox::information(
            this, tr("Apply Patch"),
            tr("Approve at least one changed diff line in the gutter before applying."));
        return;
    }

    const QString diffToApply = diffPreview->approvedDiff().trimmed();
    if (diffToApply.isEmpty())
    {
        QMessageBox::information(this, tr("Apply Patch"), tr("No approved changes to apply."));
        return;
    }

    const QString workDir = m_sessionController->currentProjectDir();

    if (workDir.isEmpty())
    {
        QMessageBox::warning(this, tr("Apply Patch"), tr("No project directory found."));
        return;
    }

    QString errorMsg;
    if (Diff::applyPatch(diffToApply, workDir, false, errorMsg))
    {
        m_appliedDiff = diffToApply;
        m_revertPatchBtn->setEnabled(true);
        onLogMessage(tr("✅ Patch applied successfully."));
    }
    else
    {
        QMessageBox::critical(this, tr("Apply Patch"), tr("Failed: %1").arg(errorMsg));
    }
}

void AgentDockWidget::onRevertPatchClicked()
{
    const QString diffToRevert = m_appliedDiff.isEmpty() ? m_currentDiff : m_appliedDiff;
    if (diffToRevert.isEmpty())
    {
        return;
    }

    const QString workDir = m_sessionController->currentProjectDir();

    QString errorMsg;
    if (Diff::revertPatch(diffToRevert, workDir, errorMsg))
    {
        m_appliedDiff.clear();
        m_revertPatchBtn->setEnabled(false);
        onLogMessage(tr("↩ Patch reverted successfully."));
    }
    else
    {
        QMessageBox::critical(this, tr("Revert Patch"), tr("Failed: %1").arg(errorMsg));
    }
}

void AgentDockWidget::onCopyPlanClicked()
{
    QStringList items;
    for (int i = 0; i < m_planList->count(); ++i)
    {
        items.append(m_planList->item(i)->text());
    }
    QGuiApplication::clipboard()->setText(items.join('\n'));
}

void AgentDockWidget::onLogMessage(const QString &msg)
{
    const QString text = msg.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    appendStampedLogEntry(text);
}

void AgentDockWidget::onProviderUsageAvailable(const ProviderUsage &usage, qint64 durationMs)
{
    const QString body = providerUsageMarkdown(usage, durationMs);
    if (body.isEmpty())
    {
        return;
    }

    appendStampedLogEntry(body);
}

void AgentDockWidget::appendStampedLogEntry(const QString &body)
{
    if (body.trimmed().isEmpty())
    {
        return;
    }

    if (!m_streamingMarkdown.isEmpty())
    {
        if (!m_logMarkdown.isEmpty())
        {
            m_logMarkdown += QStringLiteral("\n\n");
        }
        m_logMarkdown += redactReadFilePayloads(m_streamingMarkdown);
        m_streamingMarkdown.clear();
        m_streamingRenderedLen = 0;
        m_isStreaming = false;
    }

    const QString stamped =
        QStringLiteral("`[%1]`  %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), body);

    if (!m_logMarkdown.isEmpty())
    {
        m_logMarkdown += QStringLiteral("\n\n");
    }
    m_logMarkdown += stamped;

    m_renderThrottle->stop();
    renderLogFull();
}

void AgentDockWidget::renderLog()
{
    if (m_isStreaming)
    {
        // During streaming: append only new tokens (fast, no re-parse)
        const QString newTokens = m_streamingMarkdown.mid(m_streamingRenderedLen);
        m_streamingRenderedLen = m_streamingMarkdown.size();
        if (!newTokens.isEmpty())
        {
            renderLogStreaming(newTokens);
        }
        return;
    }

    renderLogFull();
}

void AgentDockWidget::renderLogFull()
{
    const QString text = currentLogMarkdown();
    const QString renderedText = wrapMarkdownPayloadBlocks(text);

    m_logView->setMarkdown(renderedText);
    m_rawMarkdownView->setPlainText(text);

    // Defer scroll to allow document layout to complete
    QTimer::singleShot(0, m_logView, [this]() {
        QTextCursor cursor = m_logView->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_logView->setTextCursor(cursor);
        m_logView->ensureCursorVisible();
    });
    QTimer::singleShot(0, m_rawMarkdownView, [this]() {
        QTextCursor cursor = m_rawMarkdownView->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_rawMarkdownView->setTextCursor(cursor);
        m_rawMarkdownView->ensureCursorVisible();
    });
}

void AgentDockWidget::renderLogStreaming(const QString &newTokens)
{
    // Append new tokens as plain text at the end of the log view
    QTextCursor cursor(m_logView->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(newTokens);
    m_logView->ensureCursorVisible();

    // Also append to raw markdown view
    QTextCursor rawCursor(m_rawMarkdownView->document());
    rawCursor.movePosition(QTextCursor::End);
    rawCursor.insertText(newTokens);
    m_rawMarkdownView->ensureCursorVisible();
}

void AgentDockWidget::onPlanUpdated(const QList<PlanStep> &steps)
{
    m_planList->clear();
    for (const auto &step : steps)
    {
        m_planList->addItem(QStringLiteral("%1. %2").arg(step.index + 1).arg(step.description));
    }
    m_sessionController->saveChat();
    m_tabs->setCurrentWidget(m_planList);
}

void AgentDockWidget::onDiffAvailable(const QString &diff)
{
    syncDiffUi(diff, true, true);
}

void AgentDockWidget::onApprovalRequested(int id, const QString &action, const QString &reason,
                                          const QString &preview)
{
    auto *item = new QListWidgetItem(QStringLiteral("[#%1] %2 — %3").arg(id).arg(action, reason));
    item->setData(Qt::UserRole, id);
    item->setToolTip(preview);
    m_approvalList->addItem(item);
    m_approvalItems[id] = item;
    m_tabs->setCurrentWidget(m_approvalList);

    // Show approval dialog
    auto result = QMessageBox::question(
        this, tr("Approval Required"),
        QStringLiteral("%1\n\nReason: %2\n\nPreview:\n%3").arg(action, reason, preview.left(1000)),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes)
    {
        item->setText(item->text() + QStringLiteral(" ✅"));
        m_controller->approveAction(id);
    }
    else
    {
        item->setText(item->text() + QStringLiteral(" ❌"));
        m_controller->denyAction(id);
    }
}

void AgentDockWidget::onIterationChanged(int iteration)
{
    onLogMessage(QStringLiteral("🔄 Iteration %1 / Tool calls: %2")
                     .arg(iteration)
                     .arg(m_controller->toolCallCount()));
}

void AgentDockWidget::onStopped(const QString &summary)
{
    updateRunState(false);
    if (m_statusLabel->text().isEmpty() == true || m_statusLabel->text() == tr("Ready") ||
        m_statusLabel->text() == tr("Running..."))
    {
        m_statusLabel->setText(QStringLiteral("Done: %1").arg(summary.left(80)));
    }
    m_sessionController->saveChat();
}

bool AgentDockWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_goalEdit && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);

        if (m_goalEdit->hasCompletionPopup())
        {
            switch (ke->key())
            {
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Escape:
                case Qt::Key_Tab:
                case Qt::Key_Backtab:
                    return false;
                default:
                    break;
            }
        }

        // Enter — run agent
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
        {
            if ((ke->modifiers() & Qt::ShiftModifier) != 0u)
            {
                return false;  // Shift+Enter — newline
            }
            onRunClicked();
            return true;
        }

        // Escape — stop agent
        if (ke->key() == Qt::Key_Escape)
        {
            onStopClicked();
            return true;
        }

        // Ctrl+L — clear log
        if (ke->key() == Qt::Key_L && ((ke->modifiers() & Qt::ControlModifier) != 0u))
        {
            m_logView->clear();
            m_rawMarkdownView->clear();
            m_logMarkdown.clear();
            m_streamingMarkdown.clear();
            m_streamingRenderedLen = 0;
            m_isStreaming = false;
            m_sessionController->saveChat();
            return true;
        }
    }
    if (obj == m_linkedFilesView && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace)
        {
            m_linkedFilesController->removeSelectedLinkedFiles();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

}  // namespace qcai2
