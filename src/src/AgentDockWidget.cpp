/*! @file
    @brief Implements the dock widget UI for chat, diff review, and approval handling.
*/

#include "AgentDockWidget.h"
#include "settings/Settings.h"
#include "ui_AgentDockWidget.h"
#include "util/Diff.h"
#include "util/Logger.h"

#include <coreplugin/editormanager/editormanager.h>
#include <projectexplorer/projectmanager.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QSizePolicy>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
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
        QStringLiteral("gpt-5.2"),
        QStringLiteral("gpt-5.3-codex"),
        QStringLiteral("gpt-5-mini"),
        QStringLiteral("gpt-4.1"),
        QStringLiteral("o3"),
        QStringLiteral("o4-mini"),
        QStringLiteral("claude-opus-4.6"),
        QStringLiteral("claude-sonnet-4.6"),
        QStringLiteral("gemini-3-flash"),
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
            continue;
        uniqueItems.append(trimmed);
    }

    const QString selected = selectedText.trimmed();
    if (!selected.isEmpty() && !uniqueItems.contains(selected))
        uniqueItems.append(selected);

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
        while (it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            rewritten += text.mid(cursor, match.capturedStart() - cursor);
            rewritten += match.captured(1);
            rewritten += QStringLiteral("\n\n~~~~text\n");
            rewritten += match.captured(2);
            if (!match.captured(2).endsWith(QLatin1Char('\n')))
                rewritten += QLatin1Char('\n');
            rewritten += QStringLiteral("~~~~");
            rewritten += match.captured(3);
            cursor = match.capturedEnd();
        }
        rewritten += text.mid(cursor);
        return rewritten;
    };

    QString rewritten = markdown;
    static const QRegularExpression toolReturnedRe(
        QStringLiteral(R"((Tool '[^']+' returned:)\n([\s\S]*?)(\n\nContinue with the next step\.))"));
    static const QRegularExpression approvedResultRe(
        QStringLiteral(R"((Approved and executed '[^']+'. Result:)\n([\s\S]*?)(\n\nContinue\.))"));

    rewritten = applyPattern(rewritten, toolReturnedRe);
    rewritten = applyPattern(rewritten, approvedResultRe);
    return rewritten;
}

QString sanitizedSessionFileName(const QString &projectFilePath)
{
    QString baseName = QFileInfo(projectFilePath).completeBaseName();
    if (baseName.isEmpty())
        baseName = QStringLiteral("session");

    baseName.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-])")),
                     QStringLiteral("_"));
    return QStringLiteral("%1.json").arg(baseName);
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
        if (text.startsWith(QStringLiteral("@@")))
            return setFormat(0, boundedInt(text.size()), m_hunkFormat);
        if (text.startsWith(QStringLiteral("+++ ")) || text.startsWith(QStringLiteral("--- ")))
            return setFormat(0, boundedInt(text.size()), m_fileHeaderFormat);
        if (text.startsWith(QStringLiteral("diff --")) ||
            text.startsWith(QStringLiteral("index ")) ||
            text.startsWith(QStringLiteral("new file")) ||
            text.startsWith(QStringLiteral("deleted file")))
            return setFormat(0, boundedInt(text.size()), m_metaFormat);
        if (text.startsWith(QLatin1Char('+')))
            return setFormat(0, boundedInt(text.size()), m_addedFormat);
        if (text.startsWith(QLatin1Char('-')))
            return setFormat(0, boundedInt(text.size()), m_removedFormat);
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
            return QString();

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
            if (!keptHunkLines.isEmpty())
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
                if (inFileSection)
                    flushFileSection();
                inFileSection = true;
                fileHeaderLines.append(lines[i]);
                ++i;
                continue;
            }

            if (!inFileSection)
            {
                outputLines.append(lines[i]);
                ++i;
                continue;
            }

            if (!lines[i].startsWith(QStringLiteral("@@ ")))
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

                if (hunkLine.startsWith(QLatin1Char('+')) &&
                    !hunkLine.startsWith(QStringLiteral("+++ ")))
                {
                    if (m_approvedLines.contains(lineNumber))
                    {
                        candidateHunk.append(hunkLine);
                        hunkHasApprovedChanges = true;
                    }
                }
                else if (hunkLine.startsWith(QLatin1Char('-')) &&
                         !hunkLine.startsWith(QStringLiteral("--- ")))
                {
                    if (m_approvedLines.contains(lineNumber))
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

            if (hunkHasApprovedChanges)
                keptHunkLines.append(candidateHunk);
        }

        if (inFileSection)
            flushFileSection();

        return Diff::normalize(outputLines.join('\n'));
    }

    void lineNumberAreaMousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
            return;

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid())
        {
            if (block.isVisible() && event->pos().y() >= top && event->pos().y() < bottom)
            {
                toggleApproval(blockNumber + 1);
                break;
            }
            if (top > event->pos().y())
                break;
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
        while (max >= 10)
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

        while (block.isValid() && top <= event->rect().bottom())
        {
            if (block.isVisible() && bottom >= event->rect().top())
            {
                const int lineNumber = blockNumber + 1;
                if (m_approvableLines.contains(lineNumber))
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
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth(0);
    }

    void recalculateApprovals()
    {
        m_approvableLines.clear();
        m_approvedLines.clear();

        for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next())
        {
            const QString text = block.text();
            if ((text.startsWith(QLatin1Char('+')) && !text.startsWith(QStringLiteral("+++ "))) ||
                (text.startsWith(QLatin1Char('-')) && !text.startsWith(QStringLiteral("--- "))))
            {
                m_approvableLines.insert(block.blockNumber() + 1);
            }
        }
    }

    void toggleApproval(int line)
    {
        if (!m_approvableLines.contains(line))
            return;

        if (m_approvedLines.contains(line))
            m_approvedLines.remove(line);
        else
            m_approvedLines.insert(line);

        notifyApprovalChanged();
        m_lineNumberArea->update();
    }

    void notifyApprovalChanged()
    {
        if (m_approvalChangedCallback)
            m_approvalChangedCallback(m_approvedLines.size(), m_approvableLines.size());
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

AgentDockWidget::AgentDockWidget(AgentController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller)
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
    connect(m_controller, &AgentController::streamingToken, this, [this](const QString &token) {
        m_streamingMarkdown += token;
        if (!m_renderThrottle->isActive())
            m_renderThrottle->start();
    });
    connect(m_controller, &AgentController::planUpdated, this, &AgentDockWidget::onPlanUpdated);
    connect(m_controller, &AgentController::diffAvailable, this,
            &AgentDockWidget::onDiffAvailable);
    connect(m_controller, &AgentController::approvalRequested, this,
            &AgentDockWidget::onApprovalRequested);
    connect(m_controller, &AgentController::iterationChanged, this,
            &AgentDockWidget::onIterationChanged);
    connect(m_controller, &AgentController::stopped, this, &AgentDockWidget::onStopped);
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
        m_debugLogView->appendPlainText(e);

    if (auto *projectManager = ProjectExplorer::ProjectManager::instance())
    {
        connect(projectManager, &ProjectExplorer::ProjectManager::projectAdded, this,
                [this](ProjectExplorer::Project *) { refreshProjectSelector(); });
        connect(projectManager, &ProjectExplorer::ProjectManager::projectRemoved, this,
                [this](ProjectExplorer::Project *) { refreshProjectSelector(); });
        connect(projectManager, &ProjectExplorer::ProjectManager::projectDisplayNameChanged, this,
                [this](ProjectExplorer::Project *) { refreshProjectSelector(); });
        connect(projectManager, &ProjectExplorer::ProjectManager::startupProjectChanged, this,
                [this](ProjectExplorer::Project *) { refreshProjectSelector(); });
    }

    refreshProjectSelector();
}

AgentDockWidget::~AgentDockWidget()
{
    saveChat();
}

void AgentDockWidget::saveChat()
{
    const QString storagePath = currentProjectStorageFilePath();
    if (storagePath.isEmpty())
        return;

    const QString persistedMarkdown = currentLogMarkdown();
    QStringList planItems;
    for (int i = 0; i < m_planList->count(); ++i)
        planItems.append(m_planList->item(i)->text());
    const auto &cfg = settings();
    const bool uiStateDiffers = m_modeCombo->currentData().toString() != QStringLiteral("agent") ||
                                m_modelCombo->currentText().trimmed() != cfg.modelName ||
                                m_reasoningCombo->currentData().toString() != cfg.reasoningEffort ||
                                m_thinkingCombo->currentData().toString() != cfg.thinkingLevel ||
                                m_dryRunCheck->isChecked() != cfg.dryRunDefault;

    const bool hasContent = !m_goalEdit->toPlainText().trimmed().isEmpty() ||
                            !persistedMarkdown.trimmed().isEmpty() || !m_currentDiff.isEmpty() ||
                            !planItems.isEmpty() || uiStateDiffers;

    if (!hasContent)
    {
        QFile::remove(storagePath);
        return;
    }

    const QFileInfo storageInfo(storagePath);
    if (!QDir().mkpath(storageInfo.absolutePath()))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to create project context dir: %1")
                              .arg(storageInfo.absolutePath()));
        return;
    }

    QJsonObject root;
    root[QStringLiteral("goal")] = m_goalEdit->toPlainText();
    root[QStringLiteral("logMarkdown")] = persistedMarkdown;
    root[QStringLiteral("diff")] = m_currentDiff;
    root[QStringLiteral("status")] = m_statusLabel->text();
    root[QStringLiteral("activeTab")] = m_tabs->currentIndex();
    root[QStringLiteral("mode")] = m_modeCombo->currentData().toString();
    root[QStringLiteral("model")] = m_modelCombo->currentText().trimmed();
    root[QStringLiteral("reasoningEffort")] = m_reasoningCombo->currentData().toString();
    root[QStringLiteral("thinkingLevel")] = m_thinkingCombo->currentData().toString();
    root[QStringLiteral("dryRun")] = m_dryRunCheck->isChecked();

    QJsonArray planJson;
    for (const QString &item : planItems)
        planJson.append(item);
    root[QStringLiteral("plan")] = planJson;

    QSaveFile file(storagePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to open project context file for writing: %1")
                              .arg(storagePath));
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        QCAI_WARN("Dock", QStringLiteral("Failed to commit project context file: %1")
                              .arg(storagePath));
    }
}

void AgentDockWidget::restoreChat()
{
    const QString storagePath = currentProjectStorageFilePath();
    if (storagePath.isEmpty())
        return;

    QFile file(storagePath);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly))
    {
        QCAI_WARN("Dock",
                  QStringLiteral("Failed to open project context file for reading: %1")
                      .arg(storagePath));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        QCAI_WARN("Dock", QStringLiteral("Invalid project context JSON: %1").arg(storagePath));
        return;
    }

    const QJsonObject root = doc.object();

    {
        const QSignalBlocker modeBlocker(m_modeCombo);
        const QSignalBlocker modelBlocker(m_modelCombo);
        const QSignalBlocker reasoningBlocker(m_reasoningCombo);
        const QSignalBlocker thinkingBlocker(m_thinkingCombo);
        const QSignalBlocker dryRunBlocker(m_dryRunCheck);

        selectEffortValue(m_reasoningCombo,
                          root.value(QStringLiteral("reasoningEffort"))
                              .toString(m_reasoningCombo->currentData().toString()),
                          m_reasoningCombo->currentIndex());
        selectEffortValue(m_thinkingCombo,
                          root.value(QStringLiteral("thinkingLevel"))
                              .toString(m_thinkingCombo->currentData().toString()),
                          m_thinkingCombo->currentIndex());

        const QString savedMode = root.value(QStringLiteral("mode")).toString();
        const int modeIndex = m_modeCombo->findData(savedMode);
        if (modeIndex >= 0)
            m_modeCombo->setCurrentIndex(modeIndex);

        const QString savedModel = root.value(QStringLiteral("model")).toString().trimmed();
        if (!savedModel.isEmpty())
            m_modelCombo->setCurrentText(savedModel);

        if (root.contains(QStringLiteral("dryRun")))
            m_dryRunCheck->setChecked(root.value(QStringLiteral("dryRun")).toBool());
    }

    const QString goal = root.value(QStringLiteral("goal")).toString();
    if (!goal.isEmpty())
        m_goalEdit->setPlainText(goal);

    const QString logMarkdown = root.value(QStringLiteral("logMarkdown")).toString();
    if (!logMarkdown.isEmpty())
    {
        // Close any unclosed code fences from interrupted streaming
        m_logMarkdown = logMarkdown;
        int fenceCount = 0;
        const auto lines = m_logMarkdown.split('\n');
        for (const auto &line : lines)
        {
            if (line.trimmed().startsWith(QStringLiteral("```")))
                ++fenceCount;
        }
        if (fenceCount % 2 != 0)
            m_logMarkdown += QStringLiteral("\n```\n");
        m_streamingMarkdown.clear();
        renderLog();
    }
    else
    {
        const QString log = root.value(QStringLiteral("log")).toString();
        if (!log.isEmpty())
        {
            m_logMarkdown = log;
            renderLog();
        }
    }

    m_currentDiff = root.value(QStringLiteral("diff")).toString();
    m_appliedDiff.clear();
    if (!m_currentDiff.isEmpty())
    {
        auto *diffPreview = static_cast<DiffPreviewEdit *>(m_diffView);
        diffPreview->setDiffText(m_currentDiff);
        m_applyPatchBtn->setEnabled(diffPreview->hasApprovedChanges());
        m_revertPatchBtn->setEnabled(false);
    }

    const QString status = root.value(QStringLiteral("status")).toString();
    if (!status.isEmpty())
        m_statusLabel->setText(status);

    const QJsonArray planItems = root.value(QStringLiteral("plan")).toArray();
    for (const auto &item : planItems)
    {
        if (item.isString())
            m_planList->addItem(item.toString());
    }

    const int tab = root.value(QStringLiteral("activeTab")).toInt(0);
    if (tab >= 0 && tab < m_tabs->count())
        m_tabs->setCurrentIndex(tab);
}

QString AgentDockWidget::currentLogMarkdown() const
{
    QString text = m_logMarkdown;
    if (!m_streamingMarkdown.isEmpty())
    {
        if (!text.isEmpty())
            text += QStringLiteral("\n\n");
        text += m_streamingMarkdown;
    }
    return text;
}

void AgentDockWidget::clearChatState()
{
    m_goalEdit->clear();
    m_logView->clear();
    m_rawMarkdownView->clear();
    m_logMarkdown.clear();
    m_streamingMarkdown.clear();
    m_planList->clear();
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
}

void AgentDockWidget::refreshProjectSelector()
{
    if (!m_projectCombo)
        return;

    const QString previousProject = m_activeProjectFilePath;
    QString desiredProject = previousProject;
    if (desiredProject.isEmpty())
    {
        if (auto *ctx = m_controller->editorContext())
        {
            desiredProject = ctx->selectedProjectFilePath();
            if (desiredProject.isEmpty())
                desiredProject = ctx->capture().projectFilePath;
        }
    }

    const QList<EditorContext::ProjectInfo> projects =
        m_controller->editorContext() ? m_controller->editorContext()->openProjects()
                                      : QList<EditorContext::ProjectInfo>{};

    const QSignalBlocker blocker(m_projectCombo);
    m_projectCombo->clear();

    if (projects.isEmpty())
    {
        m_projectCombo->addItem(tr("No open projects"), QString());
        m_projectCombo->setEnabled(false);
        switchProjectContext(QString());
        updateRunState(m_stopBtn->isEnabled());
        return;
    }

    QSet<QString> duplicateNames;
    QSet<QString> seenNames;
    for (const auto &project : projects)
    {
        if (seenNames.contains(project.projectName))
            duplicateNames.insert(project.projectName);
        seenNames.insert(project.projectName);
    }

    int selectedIndex = -1;
    for (int i = 0; i < projects.size(); ++i)
    {
        const auto &project = projects.at(i);
        QString label = project.projectName;
        if (duplicateNames.contains(project.projectName))
            label = QStringLiteral("%1 — %2")
                        .arg(project.projectName, QFileInfo(project.projectDir).fileName());
        m_projectCombo->addItem(label, project.projectFilePath);
        m_projectCombo->setItemData(i, project.projectDir, Qt::ToolTipRole);
        if (project.projectFilePath == desiredProject)
            selectedIndex = i;
    }

    if (selectedIndex < 0)
        selectedIndex = 0;

    m_projectCombo->setEnabled(true);
    m_projectCombo->setCurrentIndex(selectedIndex);

    const QString selectedProject = m_projectCombo->currentData().toString();
    if (selectedProject != m_activeProjectFilePath)
        switchProjectContext(selectedProject);
    updateRunState(m_stopBtn->isEnabled());
}

void AgentDockWidget::switchProjectContext(const QString &projectFilePath)
{
    if (projectFilePath == m_activeProjectFilePath)
        return;

    if (!m_activeProjectFilePath.isEmpty())
        saveChat();

    clearChatState();
    applyProjectUiDefaults();
    m_activeProjectFilePath = projectFilePath;

    if (auto *ctx = m_controller->editorContext())
        ctx->setSelectedProjectFilePath(projectFilePath);

    restoreChat();
}

QString AgentDockWidget::currentProjectFilePath() const
{
    return m_activeProjectFilePath;
}

QString AgentDockWidget::currentProjectStorageFilePath() const
{
    if (m_activeProjectFilePath.isEmpty())
        return {};

    QString projectDir;
    if (auto *ctx = m_controller->editorContext())
    {
        const auto projects = ctx->openProjects();
        for (const auto &project : projects)
        {
            if (project.projectFilePath == m_activeProjectFilePath)
            {
                projectDir = project.projectDir;
                break;
            }
        }
    }

    if (projectDir.isEmpty())
        projectDir = QFileInfo(m_activeProjectFilePath).absolutePath();
    if (projectDir.isEmpty())
        return {};

    return QDir(projectDir)
        .filePath(QStringLiteral(".qcai2/%1").arg(sanitizedSessionFileName(m_activeProjectFilePath)));
}

void AgentDockWidget::applyProjectUiDefaults()
{
    const auto &cfg = settings();
    const QSignalBlocker modeBlocker(m_modeCombo);
    const QSignalBlocker modelBlocker(m_modelCombo);
    const QSignalBlocker reasoningBlocker(m_reasoningCombo);
    const QSignalBlocker thinkingBlocker(m_thinkingCombo);
    const QSignalBlocker dryRunBlocker(m_dryRunCheck);

    const int modeIndex = m_modeCombo->findData(QStringLiteral("agent"));
    m_modeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    m_modelCombo->setCurrentText(cfg.modelName);
    selectEffortValue(m_reasoningCombo, cfg.reasoningEffort, 2);
    selectEffortValue(m_thinkingCombo, cfg.thinkingLevel, 2);
    m_dryRunCheck->setChecked(cfg.dryRunDefault);
}

void AgentDockWidget::setupUi()
{
    m_ui = std::make_unique<Ui::AgentDockWidget>();
    m_ui->setupUi(this);

    m_statusLabel = m_ui->statusLabel;
    m_projectCombo = m_ui->projectCombo;
    m_goalEdit = m_ui->goalEdit;
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

    auto *diffPreview = new DiffPreviewEdit(m_ui->diffViewPlaceholder->parentWidget());
    diffPreview->setObjectName(QStringLiteral("diffView"));
    diffPreview->setReadOnly(true);
    diffPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    diffPreview->setFont(QFont(QStringLiteral("monospace")));
    if (auto *layout = m_ui->diffViewPlaceholder->parentWidget()->layout())
        layout->replaceWidget(m_ui->diffViewPlaceholder, diffPreview);
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
                    return;
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
            onStopClicked();

        clearChatState();
        saveChat();
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
            return;
        switchProjectContext(m_projectCombo->itemData(index).toString());
    });
    const auto persistProjectUiState = [this]() {
        if (!m_activeProjectFilePath.isEmpty())
            saveChat();
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

    applyProjectUiDefaults();

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

void AgentDockWidget::onRunClicked()
{
    const QString goal = m_goalEdit->toPlainText().trimmed();
    if (goal.isEmpty())
        return;

    const QString selectedModel = m_modelCombo->currentText().trimmed();
    const QString selectedMode = m_modeCombo->currentData().toString();
    m_goalEdit->clear();

    updateRunState(true);
    m_tabs->setCurrentWidget(m_logView);
    m_controller->start(
        goal, m_dryRunCheck->isChecked(),
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
        return;

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

    // Get project dir from controller's editor context
    QString workDir;
    if (auto *ctx = m_controller->editorContext())
        workDir = ctx->capture().projectDir;

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
        return;

    QString workDir;
    if (auto *ctx = m_controller->editorContext())
        workDir = ctx->capture().projectDir;

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
        items.append(m_planList->item(i)->text());
    QGuiApplication::clipboard()->setText(items.join('\n'));
}

void AgentDockWidget::onLogMessage(const QString &msg)
{
    const QString text = msg.trimmed();
    if (text.isEmpty())
        return;

    if (!m_streamingMarkdown.isEmpty())
    {
        if (!m_logMarkdown.isEmpty())
            m_logMarkdown += QStringLiteral("\n\n");
        m_logMarkdown += m_streamingMarkdown;
        m_streamingMarkdown.clear();
    }

    const QString stamped =
        QStringLiteral("`[%1]`  %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), text);

    if (!m_logMarkdown.isEmpty())
        m_logMarkdown += QStringLiteral("\n\n");
    m_logMarkdown += stamped;

    m_renderThrottle->stop();
    renderLog();
}

void AgentDockWidget::renderLog()
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

void AgentDockWidget::onPlanUpdated(const QList<PlanStep> &steps)
{
    m_planList->clear();
    for (const auto &step : steps)
    {
        m_planList->addItem(QStringLiteral("%1. %2").arg(step.index + 1).arg(step.description));
    }
    saveChat();
    m_tabs->setCurrentWidget(m_planList);
}

void AgentDockWidget::onDiffAvailable(const QString &diff)
{
    m_currentDiff = diff;
    m_appliedDiff.clear();
    auto *diffPreview = static_cast<DiffPreviewEdit *>(m_diffView);
    diffPreview->setDiffText(diff);
    m_applyPatchBtn->setEnabled(!diff.isEmpty() && diffPreview->hasApprovedChanges());

    // Switch to Diff tab (m_diffView is inside a container page)
    if (auto *page = m_diffView->parentWidget())
        m_tabs->setCurrentWidget(page);

    // Populate file list from diff headers
    m_diffFileList->clear();
    QString workDir;
    if (auto *ctx = m_controller->editorContext())
        workDir = ctx->capture().projectDir;

    if (!diff.isEmpty())
    {
        // Extract file paths from "--- a/..." headers
        static const QRegularExpression reFile(QStringLiteral(R"(^--- a/(.+)$)"),
                                               QRegularExpression::MultilineOption);
        auto it = reFile.globalMatch(diff);
        QStringList files;
        while (it.hasNext())
        {
            const QString f = it.next().captured(1);
            if (!files.contains(f))
                files.append(f);
        }

        for (const auto &f : files)
        {
            auto *item = new QListWidgetItem(f);
            item->setData(Qt::UserRole, QDir(workDir).absoluteFilePath(f));
            m_diffFileList->addItem(item);
        }

        // Click → open in editor
        disconnect(m_diffFileList, &QListWidget::itemClicked, nullptr, nullptr);
        connect(m_diffFileList, &QListWidget::itemClicked, this, [](QListWidgetItem *item) {
            const QString absPath = item->data(Qt::UserRole).toString();
            Core::EditorManager::openEditorAt(
                Utils::Link(Utils::FilePath::fromString(absPath), 0, 0));
        });

        // Show inline diff markers in code editors
        if (!workDir.isEmpty())
            m_inlineDiffManager->showDiff(diff, workDir);
    }

    m_diffFileList->setVisible(!m_diffFileList->count() == 0);
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
    m_statusLabel->setText(QStringLiteral("Iteration %1 / Tool calls: %2")
                               .arg(iteration)
                               .arg(m_controller->toolCallCount()));
    onLogMessage(QStringLiteral("🔄 Iteration %1 / Tool calls: %2")
                     .arg(iteration)
                     .arg(m_controller->toolCallCount()));
}

void AgentDockWidget::onStopped(const QString &summary)
{
    updateRunState(false);
    m_statusLabel->setText(QStringLiteral("Done: %1").arg(summary.left(80)));
    saveChat();
}

bool AgentDockWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_goalEdit && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);

        // Enter — run agent
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
        {
            if (ke->modifiers() & Qt::ShiftModifier)
                return false;  // Shift+Enter — newline
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
        if (ke->key() == Qt::Key_L && (ke->modifiers() & Qt::ControlModifier))
        {
            m_logView->clear();
            m_rawMarkdownView->clear();
            m_logMarkdown.clear();
            m_streamingMarkdown.clear();
            saveChat();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

}  // namespace qcai2
