/*! @file
    @brief Implements the dock widget UI for chat, diff review, and approval handling.
*/

#include "AgentDockWidget.h"
#include "settings/Settings.h"
#include "util/Diff.h"
#include "util/Logger.h"

#include <coreplugin/editormanager/editormanager.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
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

    // Restore previous chat session
    restoreChat();
}

AgentDockWidget::~AgentDockWidget()
{
    saveChat();
}

void AgentDockWidget::saveChat()
{
    QSettings s;
    s.beginGroup(QStringLiteral("Qcai2Chat"));
    s.setValue(QStringLiteral("goal"), m_goalEdit->toPlainText());
    s.setValue(QStringLiteral("log"), m_logView->toPlainText());
    QString persistedMarkdown = m_logMarkdown;
    if (!m_streamingMarkdown.isEmpty())
    {
        if (!persistedMarkdown.isEmpty())
            persistedMarkdown += QStringLiteral("\n\n");
        persistedMarkdown += m_streamingMarkdown;
    }
    s.setValue(QStringLiteral("logMarkdown"), persistedMarkdown);
    s.setValue(QStringLiteral("diff"), m_currentDiff);
    s.setValue(QStringLiteral("status"), m_statusLabel->text());

    QStringList planItems;
    for (int i = 0; i < m_planList->count(); ++i)
        planItems.append(m_planList->item(i)->text());
    s.setValue(QStringLiteral("plan"), planItems);

    s.setValue(QStringLiteral("activeTab"), m_tabs->currentIndex());
    s.endGroup();
}

void AgentDockWidget::restoreChat()
{
    QSettings s;
    s.beginGroup(QStringLiteral("Qcai2Chat"));

    const QString goal = s.value(QStringLiteral("goal")).toString();
    if (!goal.isEmpty())
        m_goalEdit->setPlainText(goal);

    const QString logMarkdown = s.value(QStringLiteral("logMarkdown")).toString();
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
        const QString log = s.value(QStringLiteral("log")).toString();
        if (!log.isEmpty())
        {
            m_logMarkdown = log;
            renderLog();
        }
    }

    m_currentDiff = s.value(QStringLiteral("diff")).toString();
    m_appliedDiff.clear();
    if (!m_currentDiff.isEmpty())
    {
        auto *diffPreview = static_cast<DiffPreviewEdit *>(m_diffView);
        diffPreview->setDiffText(m_currentDiff);
        m_applyPatchBtn->setEnabled(diffPreview->hasApprovedChanges());
        m_revertPatchBtn->setEnabled(false);
    }

    const QString status = s.value(QStringLiteral("status")).toString();
    if (!status.isEmpty())
        m_statusLabel->setText(status);

    const QStringList planItems = s.value(QStringLiteral("plan")).toStringList();
    for (const auto &item : planItems)
        m_planList->addItem(item);

    const int tab = s.value(QStringLiteral("activeTab"), 0).toInt();
    if (tab >= 0 && tab < m_tabs->count())
        m_tabs->setCurrentIndex(tab);

    s.endGroup();
}

void AgentDockWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Status bar at top
    auto *topBar = new QHBoxLayout;
    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #888;"));
    m_applyPatchBtn = new QPushButton(tr("Apply Patch"));
    m_revertPatchBtn = new QPushButton(tr("Revert Patch"));
    auto *newChatBtn = new QPushButton(tr("New Chat"));
    m_copyPlanBtn = new QPushButton(tr("Copy Plan"));
    m_applyPatchBtn->setEnabled(false);
    m_revertPatchBtn->setEnabled(false);
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

        m_goalEdit->clear();
        m_logView->clear();
        m_logMarkdown.clear();
        m_streamingMarkdown.clear();
        m_planList->clear();
        static_cast<DiffPreviewEdit *>(m_diffView)->setDiffText(QString());
        m_approvalList->clear();
        m_approvalItems.clear();
        m_appliedDiff.clear();
        m_currentDiff.clear();
        m_statusLabel->setText(tr("Ready"));
        m_applyPatchBtn->setEnabled(false);
        m_revertPatchBtn->setEnabled(false);
        m_tabs->setCurrentWidget(m_planList);

        QSettings s;
        s.beginGroup(QStringLiteral("Qcai2Chat"));
        s.remove(QStringLiteral(""));
        s.endGroup();
    });
    connect(m_copyPlanBtn, &QPushButton::clicked, this, &AgentDockWidget::onCopyPlanClicked);
    topBar->addWidget(m_statusLabel);
    topBar->addStretch();
    topBar->addWidget(newChatBtn);
    topBar->addWidget(m_applyPatchBtn);
    topBar->addWidget(m_revertPatchBtn);
    topBar->addWidget(m_copyPlanBtn);

    // Tabs (main content area, takes all available space)
    m_tabs = new QTabWidget;

    m_planList = new QListWidget;
    m_tabs->addTab(m_planList, tr("Plan"));

    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_logView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_logView->setStyleSheet(QStringLiteral("QTextEdit { padding: 8px; }"));
    m_tabs->addTab(m_logView, tr("Actions Log"));

    m_diffView = new DiffPreviewEdit;
    m_diffView->setReadOnly(true);
    m_diffView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_diffView->setFont(QFont(QStringLiteral("monospace")));

    m_diffFileList = new QListWidget;
    m_diffFileList->setMaximumHeight(100);
    m_diffFileList->setStyleSheet(QStringLiteral(
        "QListWidget::item { padding: 2px 6px; color: #4078c0; }"
        "QListWidget::item:hover { text-decoration: underline; background: #e8f0fe; }"));
    m_diffFileList->setCursor(Qt::PointingHandCursor);

    auto *diffPage = new QWidget;
    auto *diffLayout = new QVBoxLayout(diffPage);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->setSpacing(0);
    diffLayout->addWidget(m_diffFileList);
    diffLayout->addWidget(m_diffView, 1);
    m_tabs->addTab(diffPage, tr("Diff Preview"));

    static_cast<DiffPreviewEdit *>(m_diffView)
        ->setApprovalChangedCallback([this](qsizetype approved, qsizetype total) {
            Q_UNUSED(total);
            m_applyPatchBtn->setEnabled(!m_currentDiff.isEmpty() && approved > 0);
        });

    m_approvalList = new QListWidget;
    m_tabs->addTab(m_approvalList, tr("Approvals"));

    m_debugLogView = new QPlainTextEdit;
    m_debugLogView->setReadOnly(true);
    m_debugLogView->setFont(QFont(QStringLiteral("monospace"), 9));
    m_debugLogView->setMaximumBlockCount(5000);
    m_tabs->addTab(m_debugLogView, tr("Debug Log"));

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
    addCopyShortcut(m_diffView);
    addCopyShortcut(m_debugLogView);

    // Goal input at the bottom (chat-like)
    m_goalEdit = new QTextEdit;
    m_goalEdit->setPlaceholderText(tr("Describe your goal…"));
    m_goalEdit->setMaximumHeight(72);
    m_goalEdit->setAcceptRichText(false);
    auto *goalCopyShortcut = new QShortcut(QKeySequence::Copy, m_goalEdit);
    goalCopyShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(goalCopyShortcut, &QShortcut::activated, m_goalEdit, &QTextEdit::copy);
    auto *goalSelectAllShortcut = new QShortcut(QKeySequence::SelectAll, m_goalEdit);
    goalSelectAllShortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(goalSelectAllShortcut, &QShortcut::activated, m_goalEdit,
                     &QTextEdit::selectAll);

    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(4);

    auto *btnColumn = new QVBoxLayout;
    btnColumn->setSpacing(2);

    m_modelCombo = new QComboBox;
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

    m_reasoningCombo = new QComboBox;
    populateEffortCombo(m_reasoningCombo);
    selectEffortValue(m_reasoningCombo, settings().reasoningEffort, 2);

    m_thinkingCombo = new QComboBox;
    populateEffortCombo(m_thinkingCombo);
    selectEffortValue(m_thinkingCombo, settings().thinkingLevel, 2);

    m_runBtn = new QPushButton(tr("▶ Run"));
    m_runBtn->setToolTip(tr("Run agent (Enter)"));
    m_stopBtn = new QPushButton(tr("⏹ Stop"));
    m_stopBtn->setToolTip(tr("Stop agent (Escape)"));
    m_stopBtn->setEnabled(false);
    m_dryRunCheck = new QCheckBox(tr("Dry-run"));
    m_dryRunCheck->setChecked(settings().dryRunDefault);

    connect(m_runBtn, &QPushButton::clicked, this, &AgentDockWidget::onRunClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &AgentDockWidget::onStopClicked);

    // Persist model/reasoning/thinking selection immediately on change
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, [](const QString &text) {
        auto &cfg = settings();
        const QString model = text.trimmed();
        if (!model.isEmpty())
        {
            cfg.modelName = model;
            cfg.save();
        }
    });
    connect(m_reasoningCombo, &QComboBox::currentIndexChanged, this, [this](int /*index*/) {
        auto &cfg = settings();
        cfg.reasoningEffort = m_reasoningCombo->currentData().toString();
        cfg.save();
    });
    connect(m_thinkingCombo, &QComboBox::currentIndexChanged, this, [this](int /*index*/) {
        auto &cfg = settings();
        cfg.thinkingLevel = m_thinkingCombo->currentData().toString();
        cfg.save();
    });

    btnColumn->addWidget(new QLabel(tr("Model")));
    btnColumn->addWidget(m_modelCombo);
    btnColumn->addWidget(new QLabel(tr("Reasoning")));
    btnColumn->addWidget(m_reasoningCombo);
    btnColumn->addWidget(new QLabel(tr("Thinking")));
    btnColumn->addWidget(m_thinkingCombo);
    btnColumn->addWidget(m_runBtn);
    btnColumn->addWidget(m_stopBtn);
    btnColumn->addWidget(m_dryRunCheck);

    inputRow->addWidget(m_goalEdit, 1);
    inputRow->addLayout(btnColumn);

    // Assemble: top bar → tabs → input row
    mainLayout->addLayout(topBar);
    mainLayout->addWidget(m_tabs, 1);
    mainLayout->addLayout(inputRow);
}

void AgentDockWidget::updateRunState(bool running)
{
    m_runBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_goalEdit->setReadOnly(running);
}

void AgentDockWidget::onRunClicked()
{
    const QString goal = m_goalEdit->toPlainText().trimmed();
    if (goal.isEmpty())
        return;

    auto &cfg = settings();
    const QString selectedModel = m_modelCombo->currentText().trimmed();
    if (!selectedModel.isEmpty())
        cfg.modelName = selectedModel;
    cfg.reasoningEffort = m_reasoningCombo->currentData().toString();
    cfg.thinkingLevel = m_thinkingCombo->currentData().toString();
    cfg.save();
    m_goalEdit->clear();

    updateRunState(true);
    m_tabs->setCurrentWidget(m_logView);
    m_controller->start(goal, m_dryRunCheck->isChecked());
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
    QString text = m_logMarkdown;
    if (!m_streamingMarkdown.isEmpty())
    {
        if (!text.isEmpty())
            text += QStringLiteral("\n\n");
        text += m_streamingMarkdown;
    }

    m_logView->setPlainText(text);

    // Defer scroll to allow document layout to complete
    QTimer::singleShot(0, m_logView, [this]() {
        QTextCursor cursor = m_logView->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_logView->setTextCursor(cursor);
        m_logView->ensureCursorVisible();
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
            m_logMarkdown.clear();
            m_streamingMarkdown.clear();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

}  // namespace qcai2
