/*! @file
    @brief Implements the dock widget UI for chat, diff review, and approval handling.
*/

#include "agent_dock_widget.h"
#include "../commands/compact_command.h"
#include "../goal/file_reference_goal_handler.h"
#include "../goal/slash_command_goal_handler.h"
#include "../linked_files/agent_dock_linked_files_controller.h"
#include "../session/agent_dock_session_controller.h"
#include "../settings/settings.h"
#include "../util/diff.h"
#include "../util/logger.h"
#include "../util/migration.h"
#include "ui_agent_dock_widget.h"

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
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidgetItem>
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

int bounded_int(qsizetype value)
{
    return static_cast<int>(std::min(value, qsizetype(std::numeric_limits<int>::max())));
}

QStringList open_ai_agent_models()
{
    return {
        QStringLiteral("gpt-5.4"),
        QStringLiteral("gpt-5.4-mini"),
        QStringLiteral("gpt-5.3-codex"),
        QStringLiteral("gpt-5.2-codex"),
        QStringLiteral("gpt-5-mini"),
        QStringLiteral("gpt-4.1"),
        QStringLiteral("o3"),
        QStringLiteral("o4-mini"),
        QStringLiteral("claude-opus-4.6"),
        QStringLiteral("claude-sonnet-4.6"),
        QStringLiteral("claude-haiku-4.5"),
        QStringLiteral("gemini-3-pro-preview"),
        QStringLiteral("gemini-3-flash"),
    };
}

QString conversation_display_title(const conversation_record_t &conversation)
{
    const QString explicit_title = conversation.title.trimmed();
    if (explicit_title.isEmpty() == false)
    {
        return explicit_title;
    }

    QDateTime created_at = QDateTime::fromString(conversation.created_at, Qt::ISODateWithMs);
    if (created_at.isValid() == false)
    {
        created_at = QDateTime::fromString(conversation.created_at, Qt::ISODate);
    }
    if (created_at.isValid() == true)
    {
        return QObject::tr("Conversation %1")
            .arg(created_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    }
    return QObject::tr("Untitled Conversation");
}

void populate_effort_combo(QComboBox *combo)
{
    combo->addItem(QObject::tr("Off"), QStringLiteral("off"));
    combo->addItem(QObject::tr("Low"), QStringLiteral("low"));
    combo->addItem(QObject::tr("Medium"), QStringLiteral("medium"));
    combo->addItem(QObject::tr("High"), QStringLiteral("high"));
}

void select_effort_value(QComboBox *combo, const QString &value, int fallback_index)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallback_index);
}

void populate_mode_combo(QComboBox *combo)
{
    combo->addItem(QObject::tr("Ask"), QStringLiteral("ask"));
    combo->addItem(QObject::tr("Agent"), QStringLiteral("agent"));
}

void repopulate_editable_combo(QComboBox *combo, const QStringList &items,
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

QString wrap_markdown_payload_blocks(const QString &markdown)
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
    static const QRegularExpression tool_returned_re(QStringLiteral(
        R"((Tool '[^']+' returned:)\n([\s\S]*?)(\n\nContinue with the next step\.))"));
    static const QRegularExpression approved_result_re(QStringLiteral(
        R"((Approved and executed '[^']+'. result_t:)\n([\s\S]*?)(\n\nContinue\.))"));

    rewritten = applyPattern(rewritten, tool_returned_re);
    rewritten = applyPattern(rewritten, approved_result_re);
    return rewritten;
}

QString redact_read_file_payloads(const QString &markdown)
{
    QString rewritten;
    qsizetype cursor = 0;
    static const QRegularExpression read_file_returned_re(QStringLiteral(
        R"(((?:Human:\s*)?Tool 'read_file' returned:)([\s\S]*?)(?=\n\nContinue with the next step\.|\n(?:Human:|Assistant:|\{"type":)|$))"));

    QRegularExpressionMatchIterator it = read_file_returned_re.globalMatch(markdown);
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

QString provider_usage_markdown(const provider_usage_t &usage, qint64 duration_ms)
{
    const QString summary = format_provider_usage_summary(usage, duration_ms);
    if (summary.isEmpty() == true)
    {
        return {};
    }

    return QStringLiteral("<span style=\"color:#7aa2f7;\"><strong>Usage:</strong> %1</span>")
        .arg(summary.toHtmlEscaped());
}

QString format_request_count(int count)
{
    return count == 1 ? QStringLiteral("1 request")
                      : QStringLiteral("%1 requests").arg(qMax(0, count));
}

QString format_usage_value_for_provider(const QString &provider_id, const provider_usage_t &usage,
                                        int request_count)
{
    if (provider_id == QStringLiteral("copilot"))
    {
        if (usage.quota_remaining_percent >= 0.0)
        {
            QString text = QStringLiteral("Premium reqs: %1% remaining")
                               .arg(usage.quota_remaining_percent, 0, 'f', 1);
            if (usage.quota_used_requests >= 0 && usage.quota_total_requests > 0)
            {
                text += QStringLiteral(" (%1/%2)")
                            .arg(usage.quota_used_requests)
                            .arg(usage.quota_total_requests);
            }
            return text;
        }
        if (request_count <= 0)
        {
            return QStringLiteral("Copilot Premium requests usage: %1").arg(QObject::tr("—"));
        }
        return QStringLiteral("Copilot Premium requests usage: %1")
            .arg(format_request_count(request_count));
    }

    const QString summary = format_provider_usage_summary(usage);
    return summary.isEmpty() == true ? QStringLiteral("—") : summary;
}

class diff_highlighter_t final : public QSyntaxHighlighter
{
public:
    explicit diff_highlighter_t(QTextDocument *document) : QSyntaxHighlighter(document)
    {
        const QPalette palette = QGuiApplication::palette();
        const bool is_dark = palette.color(QPalette::Base).lightness() < 128;
        const QColor text_color = palette.color(QPalette::Text);

        this->added_format.setForeground(text_color);
        this->added_format.setBackground(is_dark ? QColor(QStringLiteral("#1f3a2a"))
                                                 : QColor(QStringLiteral("#e8f5e9")));
        this->removed_format.setForeground(text_color);
        this->removed_format.setBackground(is_dark ? QColor(QStringLiteral("#3b2424"))
                                                   : QColor(QStringLiteral("#fdeaea")));
        this->hunk_format.setForeground(is_dark ? QColor(QStringLiteral("#9ecbff"))
                                                : QColor(QStringLiteral("#0b4f9e")));
        this->hunk_format.setBackground(is_dark ? QColor(QStringLiteral("#1d2f45"))
                                                : QColor(QStringLiteral("#e7f1ff")));
        this->hunk_format.setFontWeight(QFont::Bold);
        this->file_header_format.setForeground(text_color);
        this->file_header_format.setBackground(is_dark ? QColor(QStringLiteral("#2f2f2f"))
                                                       : QColor(QStringLiteral("#ececec")));
        this->meta_format.setForeground(is_dark ? QColor(QStringLiteral("#b7beca"))
                                                : QColor(QStringLiteral("#566070")));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (((text.startsWith(QStringLiteral("@@"))) == true))
        {
            return setFormat(0, bounded_int(text.size()), this->hunk_format);
        }
        if (((text.startsWith(QStringLiteral("+++ ")) ||
              text.startsWith(QStringLiteral("--- "))) == true))
        {
            return setFormat(0, bounded_int(text.size()), this->file_header_format);
        }
        if (((text.startsWith(QStringLiteral("diff --")) ||
              text.startsWith(QStringLiteral("index ")) ||
              text.startsWith(QStringLiteral("new file")) ||
              text.startsWith(QStringLiteral("deleted file"))) == true))
        {
            return setFormat(0, bounded_int(text.size()), this->meta_format);
        }
        if (((text.startsWith(QLatin1Char('+'))) == true))
        {
            return setFormat(0, bounded_int(text.size()), this->added_format);
        }
        if (((text.startsWith(QLatin1Char('-'))) == true))
        {
            return setFormat(0, bounded_int(text.size()), this->removed_format);
        }
    }

private:
    QTextCharFormat added_format;
    QTextCharFormat removed_format;
    QTextCharFormat hunk_format;
    QTextCharFormat file_header_format;
    QTextCharFormat meta_format;
};

class diff_preview_edit_t;

class diff_line_number_area_t final : public QWidget
{
public:
    explicit diff_line_number_area_t(diff_preview_edit_t *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    diff_preview_edit_t *editor;
};

class diff_preview_edit_t final : public QPlainTextEdit
{
public:
    explicit diff_preview_edit_t(QWidget *parent = nullptr)
        : QPlainTextEdit(parent), line_number_area(new diff_line_number_area_t(this)),
          highlighter(document())
    {
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                &diff_preview_edit_t::update_line_number_area_width);
        connect(this, &QPlainTextEdit::updateRequest, this,
                &diff_preview_edit_t::update_line_number_area);
        this->update_line_number_area_width(0);
    }

    bool all_changes_approved() const
    {
        return this->approvable_lines.isEmpty() ||
               this->approved_lines.size() == this->approvable_lines.size();
    }

    bool has_approved_changes() const
    {
        return !this->approved_lines.isEmpty();
    }

    void set_diff_text(const QString &diff)
    {
        setPlainText(diff);
        this->recalculate_approvals();
        this->notify_approval_changed();
        this->line_number_area->update();
    }

    void set_approval_changed_callback(std::function<void(qsizetype, qsizetype)> callback)
    {
        this->approval_changed_callback = std::move(callback);
        this->notify_approval_changed();
    }

    QString approved_diff() const
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
                    if (this->approved_lines.contains(lineNumber) == true)
                    {
                        candidateHunk.append(hunkLine);
                        hunkHasApprovedChanges = true;
                    }
                }
                else if (((hunkLine.startsWith(QLatin1Char('-')) &&
                           !hunkLine.startsWith(QStringLiteral("--- "))) == true))
                {
                    if (this->approved_lines.contains(lineNumber) == true)
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

    void line_number_area_mouse_press_event(QMouseEvent *event)
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
                this->toggle_approval(blockNumber + 1);
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

    int line_number_area_width() const
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

    void line_number_area_paint_event(QPaintEvent *event)
    {
        QPainter painter(this->line_number_area);
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
                if (this->approvable_lines.contains(lineNumber) == true)
                {
                    const QRect markerRect(2, top + (fontMetrics().height() - 9) / 2, 9, 9);
                    painter.setPen(palette().color(QPalette::Mid));
                    painter.setBrush(this->approved_lines.contains(lineNumber)
                                         ? QBrush(QColor(QStringLiteral("#4caf50")))
                                         : Qt::NoBrush);
                    painter.drawRect(markerRect);
                }

                const QString number = QString::number(blockNumber + 1);
                painter.drawText(14, top, this->line_number_area->width() - 18,
                                 fontMetrics().height(), Qt::AlignRight | Qt::AlignVCenter,
                                 number);
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
        this->line_number_area->setGeometry(
            QRect(cr.left(), cr.top(), this->line_number_area_width(), cr.height()));
    }

private:
    void update_line_number_area_width(int)
    {
        setViewportMargins(this->line_number_area_width(), 0, 0, 0);
    }
    void update_line_number_area(const QRect &rect, int dy)
    {
        if (((dy != 0) == true))
        {
            this->line_number_area->scroll(0, dy);
        }
        else
        {
            this->line_number_area->update(0, rect.y(), this->line_number_area->width(),
                                           rect.height());
        }
        if (rect.contains(viewport()->rect()))
        {
            this->update_line_number_area_width(0);
        }
    }

    void recalculate_approvals()
    {
        this->approvable_lines.clear();
        this->approved_lines.clear();

        for (QTextBlock block = this->document()->firstBlock(); ((block.isValid()) == true);
             block = block.next())
        {
            const QString text = block.text();
            if ((((text.startsWith(QLatin1Char('+')) &&
                   !text.startsWith(QStringLiteral("+++ "))) ||
                  (text.startsWith(QLatin1Char('-')) &&
                   !text.startsWith(QStringLiteral("--- ")))) == true))
            {
                this->approvable_lines.insert(block.blockNumber() + 1);
            }
        }
    }

    void toggle_approval(int line)
    {
        if (((!this->approvable_lines.contains(line)) == true))
        {
            return;
        }

        if (this->approved_lines.contains(line) == true)
        {
            this->approved_lines.remove(line);
        }
        else
        {
            this->approved_lines.insert(line);
        }

        this->notify_approval_changed();
        this->line_number_area->update();
    }

    void notify_approval_changed()
    {
        if (static_cast<bool>(this->approval_changed_callback) == true)
        {
            this->approval_changed_callback(this->approved_lines.size(),
                                            this->approvable_lines.size());
        }
    }

    diff_line_number_area_t *line_number_area;
    diff_highlighter_t highlighter;
    QSet<int> approvable_lines;
    QSet<int> approved_lines;
    std::function<void(qsizetype, qsizetype)> approval_changed_callback;
};

diff_line_number_area_t::diff_line_number_area_t(diff_preview_edit_t *editor)
    : QWidget(editor), editor(editor)
{
}
QSize diff_line_number_area_t::sizeHint() const
{
    return QSize(this->editor->line_number_area_width(), 0);
}
void diff_line_number_area_t::paintEvent(QPaintEvent *event)
{
    this->editor->line_number_area_paint_event(event);
}
void diff_line_number_area_t::mousePressEvent(QMouseEvent *event)
{
    this->editor->line_number_area_mouse_press_event(event);
}

}  // namespace

agent_dock_widget_t::agent_dock_widget_t(agent_controller_t *controller,
                                         chat_context_manager_t *chat_context_manager,
                                         QWidget *parent)
    : QWidget(parent), controller(controller),
      linked_files_controller(std::make_unique<agent_dock_linked_files_controller_t>(*this)),
      session_controller(
          std::make_unique<agent_dock_session_controller_t>(*this, chat_context_manager))
{
    this->setup_ui();

    this->inline_diff_manager = new inline_diff_manager_t(this);

    // Throttle rendering during streaming to avoid O(n²) setPlainText() calls
    this->render_throttle = new QTimer(this);
    this->render_throttle->setSingleShot(true);
    this->render_throttle->setInterval(50);
    connect(this->render_throttle, &QTimer::timeout, this, &agent_dock_widget_t::render_log);

    // Connect controller signals
    connect(this->controller, &agent_controller_t::log_message, this,
            &agent_dock_widget_t::on_log_message);
    connect(this->controller, &agent_controller_t::provider_usage_available, this,
            &agent_dock_widget_t::on_provider_usage_available);
    connect(this->controller, &agent_controller_t::status_changed, this,
            [this](const QString &status) { this->status_label->setText(status); });
    connect(this->controller, &agent_controller_t::streaming_token, this,
            [this](const QString &token) {
                this->streaming_response_raw += token;
                this->streaming_markdown =
                    streaming_response_markdown_preview(this->streaming_response_raw);
                if (!this->is_streaming)
                {
                    this->is_streaming = true;
                    this->streaming_rendered_len = 0;
                }
                if (!this->render_throttle->isActive())
                {
                    this->render_throttle->start();
                }
            });
    connect(this->controller, &agent_controller_t::plan_updated, this,
            &agent_dock_widget_t::on_plan_updated);
    connect(this->controller, &agent_controller_t::diff_available, this,
            &agent_dock_widget_t::on_diff_available);
    connect(this->controller, &agent_controller_t::approval_requested, this,
            &agent_dock_widget_t::on_approval_requested);
    connect(this->controller, &agent_controller_t::iteration_changed, this,
            &agent_dock_widget_t::on_iteration_changed);
    connect(this->controller, &agent_controller_t::stopped, this,
            &agent_dock_widget_t::on_stopped);
    connect(this->inline_diff_manager, &inline_diff_manager_t::diff_changed, this,
            [this](const QString &diff) {
                this->sync_diff_ui(diff, false, false);
                this->session_controller->save_chat();
            });
    connect(this->inline_diff_manager, &inline_diff_manager_t::hunk_accepted, this,
            [this](int, const QString &file_path) {
                this->on_log_message(QStringLiteral("✅ Inline diff accepted: %1").arg(file_path));
            });
    connect(this->inline_diff_manager, &inline_diff_manager_t::hunk_rejected, this,
            [this](int, const QString &file_path) {
                this->on_log_message(QStringLiteral("❌ Inline diff rejected: %1").arg(file_path));
            });
    connect(this->inline_diff_manager, &inline_diff_manager_t::all_resolved, this, [this]() {
        this->on_log_message(QStringLiteral("✅ All inline diff hunks resolved."));
    });
    connect(this->controller, &agent_controller_t::error_occurred, this,
            [this](const QString &err) {
                this->on_log_message(QStringLiteral("❌ Error: %1").arg(err));
                this->update_run_state(false);
            });

    // All hotkeys are handled via event filter on goal edit only
    this->goal_edit->installEventFilter(this);

    // Connect debug logger to Debug Log tab
    connect(&logger_t::instance(), &logger_t::entry_added, this,
            [this](const QString &entry) { this->debug_log_view->appendPlainText(entry); });
    // Load existing log entries
    const auto existing_entries = logger_t::instance().log_entries();
    for (const auto &e : existing_entries)
    {
        this->debug_log_view->appendPlainText(e);
    }

    if (auto *project_manager = ProjectExplorer::ProjectManager::instance())
    {
        connect(project_manager, &ProjectExplorer::ProjectManager::projectAdded, this,
                [this](ProjectExplorer::Project *) {
                    this->session_controller->refresh_project_selector();
                });
        connect(project_manager, &ProjectExplorer::ProjectManager::projectRemoved, this,
                [this](ProjectExplorer::Project *) {
                    this->session_controller->refresh_project_selector();
                });
        connect(project_manager, &ProjectExplorer::ProjectManager::projectDisplayNameChanged, this,
                [this](ProjectExplorer::Project *) {
                    this->session_controller->refresh_project_selector();
                });
        connect(project_manager, &ProjectExplorer::ProjectManager::startupProjectChanged, this,
                [this](ProjectExplorer::Project *) {
                    this->session_controller->refresh_project_selector();
                });
    }

    if (auto *editor_manager = Core::EditorManager::instance())
    {
        connect(editor_manager, &Core::EditorManager::currentEditorChanged, this,
                [this](Core::IEditor *) { this->linked_files_controller->refresh_ui(); });
    }

    this->session_controller->refresh_project_selector();
}

agent_dock_widget_t::~agent_dock_widget_t()
{
    this->session_controller->save_chat();
}

void agent_dock_widget_t::focus_goal_input()
{
    if (this->goal_edit == nullptr)
    {
        return;
    }

    this->goal_edit->setFocus(Qt::ShortcutFocusReason);
    QTextCursor cursor = this->goal_edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    this->goal_edit->setTextCursor(cursor);
}

QString agent_dock_widget_t::current_log_markdown() const
{
    QString text = this->log_markdown;
    if (!this->streaming_markdown.isEmpty())
    {
        if (!text.isEmpty())
        {
            text += QStringLiteral("\n\n");
        }
        text += this->streaming_markdown;
    }
    return redact_read_file_payloads(text);
}

void agent_dock_widget_t::sync_diff_ui(const QString &diff, bool focusDiffTab,
                                       bool refreshInlineMarkers)
{
    this->current_diff = diff;
    this->applied_diff.clear();
    this->revert_patch_btn->setEnabled(false);

    auto *diff_preview = static_cast<diff_preview_edit_t *>(this->diff_view);
    diff_preview->set_diff_text(diff);
    this->apply_patch_btn->setEnabled(!diff.isEmpty() && diff_preview->has_approved_changes());

    if (focusDiffTab)
    {
        if (auto *page = this->diff_view->parentWidget())
        {
            this->tabs->setCurrentWidget(page);
        }
    }

    this->diff_file_list->clear();
    const QString work_dir = this->session_controller->current_project_dir();

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
            item->setData(Qt::UserRole, QDir(work_dir).absoluteFilePath(f));
            this->diff_file_list->addItem(item);
        }

        disconnect(this->diff_file_list, &QListWidget::itemClicked, nullptr, nullptr);
        connect(this->diff_file_list, &QListWidget::itemClicked, this, [](QListWidgetItem *item) {
            const QString absPath = item->data(Qt::UserRole).toString();
            Core::EditorManager::openEditorAt(
                Utils::Link(Utils::FilePath::fromString(absPath), 0, 0));
        });
    }

    if (refreshInlineMarkers)
    {
        if (!diff.isEmpty() && !work_dir.isEmpty())
        {
            this->inline_diff_manager->show_diff(diff, work_dir);
        }
        else
        {
            this->inline_diff_manager->clear_all();
        }
    }

    this->diff_file_list->setVisible(this->diff_file_list->count() != 0);
}

void agent_dock_widget_t::clear_chat_state()
{
    this->goal_edit->clear();
    this->linked_files_controller->clear_state();
    this->log_view->clear();
    this->raw_markdown_view->clear();
    this->log_markdown.clear();
    this->streaming_markdown.clear();
    this->streaming_response_raw.clear();
    this->last_committed_streaming_markdown.clear();
    this->streaming_rendered_len = 0;
    this->is_streaming = false;
    this->plan_list->clear();
    this->inline_diff_manager->clear_all();
    static_cast<diff_preview_edit_t *>(this->diff_view)->set_diff_text(QString());
    this->diff_file_list->clear();
    this->approval_list->clear();
    this->approval_items.clear();
    this->applied_diff.clear();
    this->current_diff.clear();
    this->status_label->setText(tr("Ready"));
    this->deferred_run_goal.clear();
    this->skip_auto_compact_once = false;
    this->reset_usage_display();
    this->apply_patch_btn->setEnabled(false);
    this->revert_patch_btn->setEnabled(false);
    this->tabs->setCurrentWidget(this->plan_list);
    this->linked_files_controller->refresh_ui();
}

void agent_dock_widget_t::refresh_conversation_list()
{
    if (this->conversation_combo == nullptr)
    {
        return;
    }

    const QList<conversation_record_t> conversations =
        this->session_controller->available_conversations();
    const QString active_conversation_id = this->session_controller->active_conversation();

    this->conversation_list_refreshing = true;
    const QSignalBlocker blocker(this->conversation_combo);
    this->conversation_combo->clear();

    int active_index = -1;
    int index = 0;
    for (const conversation_record_t &conversation : conversations)
    {
        this->conversation_combo->addItem(conversation_display_title(conversation),
                                          conversation.conversation_id);
        this->conversation_combo->setItemData(index, conversation.title, Qt::UserRole + 1);
        this->conversation_combo->setItemData(
            index,
            tr("Created: %1\nUpdated: %2")
                .arg(conversation.created_at.isEmpty() ? tr("Unknown") : conversation.created_at,
                     conversation.updated_at.isEmpty() ? tr("Unknown") : conversation.updated_at),
            Qt::ToolTipRole);
        if (conversation.conversation_id == active_conversation_id)
        {
            active_index = index;
        }
        ++index;
    }

    if (active_index >= 0)
    {
        this->conversation_combo->setCurrentIndex(active_index);
    }
    else if (this->conversation_combo->count() > 0)
    {
        this->conversation_combo->setCurrentIndex(0);
    }

    const bool has_project =
        this->session_controller->current_project_file_path().isEmpty() == false;
    this->conversation_combo->setEnabled(has_project && this->stop_btn->isEnabled() == false);
    this->rename_conversation_btn->setEnabled(this->conversation_combo->currentIndex() >= 0 &&
                                              this->stop_btn->isEnabled() == false);
    this->delete_conversation_btn->setEnabled(this->conversation_combo->currentIndex() >= 0 &&
                                              this->stop_btn->isEnabled() == false);
    this->conversation_list_refreshing = false;
}

void agent_dock_widget_t::rename_selected_conversation()
{
    if (this->conversation_combo == nullptr || this->conversation_combo->currentIndex() < 0)
    {
        return;
    }

    const int index = this->conversation_combo->currentIndex();
    const QString conversation_id = this->conversation_combo->itemData(index).toString();
    if (conversation_id.isEmpty() == true)
    {
        return;
    }

    bool ok = false;
    const QString explicit_title =
        this->conversation_combo->itemData(index, Qt::UserRole + 1).toString().trimmed();
    const QString initial_title = explicit_title.isEmpty() == true
                                      ? this->conversation_combo->itemText(index)
                                      : explicit_title;
    const QString title = QInputDialog::getText(
        this, tr("Rename Conversation"),
        tr("Conversation title (leave empty to use the default generated title):"),
        QLineEdit::Normal, initial_title, &ok);
    if (ok == false)
    {
        return;
    }

    QString error;
    if (this->session_controller->rename_conversation(conversation_id, title, &error) == false)
    {
        QMessageBox::warning(this, tr("Rename Conversation"),
                             error.isEmpty() ? tr("Failed to rename conversation.") : error);
        return;
    }
    this->refresh_conversation_list();
}

void agent_dock_widget_t::delete_selected_conversation()
{
    if (this->conversation_combo == nullptr || this->conversation_combo->currentIndex() < 0)
    {
        return;
    }

    const int index = this->conversation_combo->currentIndex();
    const QString conversation_id = this->conversation_combo->itemData(index).toString();
    if (conversation_id.isEmpty() == true)
    {
        return;
    }

    if (QMessageBox::question(this, tr("Delete Conversation"),
                              tr("Delete the current conversation? This removes its saved "
                                 "messages and local state."),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    QString error;
    if (this->session_controller->delete_conversation(conversation_id, &error) == false)
    {
        QMessageBox::warning(this, tr("Delete Conversation"),
                             error.isEmpty() ? tr("Failed to delete conversation.") : error);
        return;
    }
    this->refresh_conversation_list();
}

void agent_dock_widget_t::setup_ui()
{
    this->ui = std::make_unique<Ui::agent_dock_widget_t>();
    this->ui->setupUi(this);

    this->status_label = this->ui->statusLabel;
    this->usage_value_label = this->ui->usageValueLabel;
    this->project_combo = this->ui->projectCombo;
    this->conversation_combo = this->ui->conversationCombo;
    auto *designer_goal_edit = this->ui->goalEdit;
    this->mode_combo = this->ui->modeCombo;
    this->model_combo = this->ui->modelCombo;
    this->reasoning_combo = this->ui->reasoningCombo;
    this->thinking_combo = this->ui->thinkingCombo;
    this->run_btn = this->ui->runBtn;
    this->stop_btn = this->ui->stopBtn;
    this->dry_run_check = this->ui->dryRunCheck;
    this->tabs = this->ui->tabs;
    this->plan_list = this->ui->planList;
    this->log_view = this->ui->logView;
    this->raw_markdown_view = this->ui->rawMarkdownView;
    this->diff_file_list = this->ui->diffFileList;
    this->approval_list = this->ui->approvalList;
    this->debug_log_view = this->ui->debugLogView;
    this->apply_patch_btn = this->ui->applyPatchBtn;
    this->revert_patch_btn = this->ui->revertPatchBtn;
    this->copy_plan_btn = this->ui->copyPlanBtn;
    this->rename_conversation_btn = this->ui->renameConversationBtn;
    this->delete_conversation_btn = this->ui->deleteConversationBtn;
    auto *new_chat_btn = this->ui->newChatButton;

    this->ui->contentSplitter->setStretchFactor(0, 3);
    this->ui->contentSplitter->setStretchFactor(1, 1);
    this->ui->inputRow->setStretch(0, 1);
    this->ui->modeModelRow->setStretch(2, 1);
    this->reset_usage_display();

    this->linked_files_view = new linked_files_list_widget_t(this);
    this->linked_files_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->linked_files_view->setVisible(false);
    this->linked_files_view->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto *removeLinkedFileAction = new QAction(tr("Remove Linked File"), this->linked_files_view);
    connect(removeLinkedFileAction, &QAction::triggered, this,
            [this]() { this->linked_files_controller->remove_selected_linked_files(); });
    this->linked_files_view->addAction(removeLinkedFileAction);
    this->ui->goalColumn->insertWidget(0, this->linked_files_view);

    auto *goal_edit = new goal_text_edit_t(designer_goal_edit->parentWidget());
    goal_edit->setObjectName(designer_goal_edit->objectName());
    goal_edit->setSizePolicy(designer_goal_edit->sizePolicy());
    goal_edit->setMinimumSize(designer_goal_edit->minimumSize());
    goal_edit->setAcceptRichText(designer_goal_edit->acceptRichText());
    goal_edit->setPlaceholderText(designer_goal_edit->placeholderText());
    goal_edit->setFont(designer_goal_edit->font());
    const int goal_edit_index = this->ui->goalColumn->indexOf(designer_goal_edit);
    this->ui->goalColumn->removeWidget(designer_goal_edit);
    this->ui->goalColumn->insertWidget(goal_edit_index, goal_edit);
    designer_goal_edit->deleteLater();
    this->goal_edit = goal_edit;
    this->goal_edit->add_special_handler(
        std::make_unique<slash_command_goal_handler_t>(&this->slash_commands));
    this->goal_edit->add_special_handler(std::make_unique<file_reference_goal_handler_t>(
        [this]() { return this->linked_files_controller->linked_file_candidates(); }));
    connect(this->goal_edit, &QTextEdit::textChanged, this,
            [this]() { this->linked_files_controller->refresh_ui(); });
    connect(this->goal_edit, &goal_text_edit_t::files_dropped, this,
            [this](const QStringList &paths) {
                this->linked_files_controller->add_linked_files(paths);
            });
    connect(this->linked_files_view, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                const QString absolutePath =
                    this->linked_files_controller->linked_file_absolute_path(
                        item->data(Qt::UserRole).toString());
                if (!absolutePath.isEmpty())
                {
                    Core::EditorManager::openEditorAt(
                        Utils::Link(Utils::FilePath::fromString(absolutePath), 0, 0));
                }
            });
    this->linked_files_view->installEventFilter(this);
    this->linked_files_controller->refresh_ui();

    auto *diff_preview = new diff_preview_edit_t(this->ui->diffViewPlaceholder->parentWidget());
    diff_preview->setObjectName(QStringLiteral("diffView"));
    diff_preview->setReadOnly(true);
    diff_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    diff_preview->setFont(QFont(QStringLiteral("monospace")));
    if (auto *layout = this->ui->diffViewPlaceholder->parentWidget()->layout())
    {
        layout->replaceWidget(this->ui->diffViewPlaceholder, diff_preview);
    }
    this->ui->diffViewPlaceholder->deleteLater();
    this->diff_view = diff_preview;

    this->log_view->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    this->raw_markdown_view->setFont(QFont(QStringLiteral("monospace"), 9));
    this->debug_log_view->setFont(QFont(QStringLiteral("monospace"), 9));

    populate_mode_combo(this->mode_combo);

    this->model_combo->setEditable(true);
    const bool use_copilot_models = settings().provider == QStringLiteral("copilot");
    repopulate_editable_combo(this->model_combo,
                              use_copilot_models ? model_catalog().copilot_models()
                                                 : open_ai_agent_models(),
                              settings().model_name);
    connect(&model_catalog(), &model_catalog_t::copilot_models_changed, this,
            [this](const QStringList &models) {
                if (settings().provider != QStringLiteral("copilot"))
                {
                    return;
                }
                repopulate_editable_combo(this->model_combo, models,
                                          this->model_combo->currentText());
            });

    populate_effort_combo(this->reasoning_combo);
    select_effort_value(this->reasoning_combo, settings().reasoning_effort, 2);
    populate_effort_combo(this->thinking_combo);
    select_effort_value(this->thinking_combo, settings().thinking_level, 2);
    this->dry_run_check->setChecked(settings().dry_run_default);

    connect(this->apply_patch_btn, &QPushButton::clicked, this,
            &agent_dock_widget_t::on_apply_patch_clicked);
    connect(this->revert_patch_btn, &QPushButton::clicked, this,
            &agent_dock_widget_t::on_revert_patch_clicked);
    connect(new_chat_btn, &QPushButton::clicked, this, [this]() {
        if (this->stop_btn->isEnabled())
        {
            this->on_stop_clicked();
        }

        this->session_controller->start_new_conversation();
        this->clear_chat_state();
        this->session_controller->save_chat();
    });
    connect(this->copy_plan_btn, &QPushButton::clicked, this,
            &agent_dock_widget_t::on_copy_plan_clicked);
    connect(this->rename_conversation_btn, &QPushButton::clicked, this,
            &agent_dock_widget_t::rename_selected_conversation);
    connect(this->delete_conversation_btn, &QPushButton::clicked, this,
            &agent_dock_widget_t::delete_selected_conversation);
    connect(this->conversation_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (this->conversation_list_refreshing || index < 0)
                {
                    return;
                }

                const QString conversation_id =
                    this->conversation_combo->itemData(index).toString();
                if (conversation_id.isEmpty() == true ||
                    conversation_id == this->session_controller->active_conversation())
                {
                    this->rename_conversation_btn->setEnabled(
                        index >= 0 && this->stop_btn->isEnabled() == false);
                    this->delete_conversation_btn->setEnabled(
                        index >= 0 && this->stop_btn->isEnabled() == false);
                    return;
                }

                if (this->stop_btn->isEnabled())
                {
                    QMessageBox::information(
                        this, tr("Conversation Switch"),
                        tr("Stop the current run before switching conversations."));
                    this->refresh_conversation_list();
                    return;
                }

                this->session_controller->switch_conversation(conversation_id);
            });

    static_cast<diff_preview_edit_t *>(this->diff_view)
        ->set_approval_changed_callback([this](qsizetype approved, qsizetype total) {
            Q_UNUSED(total);
            this->apply_patch_btn->setEnabled(!this->current_diff.isEmpty() && approved > 0);
        });

    connect(this->run_btn, &QPushButton::clicked, this, &agent_dock_widget_t::on_run_clicked);
    connect(this->stop_btn, &QPushButton::clicked, this, &agent_dock_widget_t::on_stop_clicked);
    connect(this->project_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
        {
            return;
        }
        this->session_controller->switch_project_context(
            this->project_combo->itemData(index).toString());
    });
    const auto persist_project_ui_state = [this]() {
        if (!this->session_controller->current_project_file_path().isEmpty())
        {
            this->session_controller->save_chat();
        }
    };
    connect(this->mode_combo, &QComboBox::currentIndexChanged, this,
            [persist_project_ui_state](int) { persist_project_ui_state(); });
    connect(this->model_combo, &QComboBox::currentTextChanged, this,
            [persist_project_ui_state](const QString &) { persist_project_ui_state(); });
    connect(this->reasoning_combo, &QComboBox::currentIndexChanged, this,
            [persist_project_ui_state](int) { persist_project_ui_state(); });
    connect(this->thinking_combo, &QComboBox::currentIndexChanged, this,
            [persist_project_ui_state](int) { persist_project_ui_state(); });
    connect(this->dry_run_check, &QCheckBox::checkStateChanged, this,
            [persist_project_ui_state](Qt::CheckState) { persist_project_ui_state(); });

    this->session_controller->apply_project_ui_defaults();

    // Add Ctrl+C/A shortcuts directly on text views (event filter not enough — Qt Creator
    // intercepts)
    auto add_copy_shortcut = [](QPlainTextEdit *edit) {
        auto *copy_shortcut = new QShortcut(QKeySequence::Copy, edit);
        copy_shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(copy_shortcut, &QShortcut::activated, edit, &QPlainTextEdit::copy);
        auto *select_all_shortcut = new QShortcut(QKeySequence::SelectAll, edit);
        select_all_shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(select_all_shortcut, &QShortcut::activated, edit,
                         &QPlainTextEdit::selectAll);
    };
    auto add_copy_shortcut_rich = [](QTextEdit *edit) {
        auto *copy_shortcut = new QShortcut(QKeySequence::Copy, edit);
        copy_shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(copy_shortcut, &QShortcut::activated, edit, &QTextEdit::copy);
        auto *select_all_shortcut = new QShortcut(QKeySequence::SelectAll, edit);
        select_all_shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(select_all_shortcut, &QShortcut::activated, edit, &QTextEdit::selectAll);
    };
    add_copy_shortcut_rich(this->log_view);
    add_copy_shortcut(this->raw_markdown_view);
    add_copy_shortcut(this->diff_view);
    add_copy_shortcut(this->debug_log_view);
    auto *goal_copy_shortcut = new QShortcut(QKeySequence::Copy, this->goal_edit);
    goal_copy_shortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(goal_copy_shortcut, &QShortcut::activated, this->goal_edit, &QTextEdit::copy);
    auto *goal_select_all_shortcut = new QShortcut(QKeySequence::SelectAll, this->goal_edit);
    goal_select_all_shortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(goal_select_all_shortcut, &QShortcut::activated, this->goal_edit,
                     &QTextEdit::selectAll);
}

void agent_dock_widget_t::update_run_state(bool running)
{
    this->run_btn->setEnabled(!running);
    this->stop_btn->setEnabled(running);
    this->goal_edit->setReadOnly(running);
    this->mode_combo->setEnabled(!running);
    this->model_combo->setEnabled(!running);
    this->reasoning_combo->setEnabled(!running);
    this->thinking_combo->setEnabled(!running);
    this->dry_run_check->setEnabled(!running);
    this->project_combo->setEnabled(!running && this->project_combo->count() > 0 &&
                                    !this->project_combo->itemData(0).toString().isEmpty());
    if (this->conversation_combo != nullptr)
    {
        this->conversation_combo->setEnabled(
            !running && this->session_controller->current_project_file_path().isEmpty() == false);
    }
    if (this->rename_conversation_btn != nullptr)
    {
        this->rename_conversation_btn->setEnabled(!running &&
                                                  this->conversation_combo != nullptr &&
                                                  this->conversation_combo->currentIndex() >= 0);
    }
    if (this->delete_conversation_btn != nullptr)
    {
        this->delete_conversation_btn->setEnabled(!running &&
                                                  this->conversation_combo != nullptr &&
                                                  this->conversation_combo->currentIndex() >= 0);
    }
}

bool agent_dock_widget_t::try_execute_slash_command(QString &goal)
{
    const slash_command_dispatch_result_t result = this->slash_commands.dispatch(
        goal, slash_command_context_t{
                  [this](const QString &message) { this->on_log_message(message); }});
    if (!result.is_slash_command)
    {
        return false;
    }

    // Redirect to AI agent when the handler produced a goal_override.
    if (!result.goal_override.isEmpty())
    {
        goal = result.goal_override;
        return false;
    }

    this->tabs->setCurrentWidget(this->log_view);

    if (!result.error_message.isEmpty())
    {
        this->on_log_message(result.error_message);
    }

    if (result.executed)
    {
        this->goal_edit->clear();
        this->status_label->setText(
            QStringLiteral("Slash command executed: /%1").arg(result.command_name));
    }

    this->session_controller->save_chat();
    return true;
}

void agent_dock_widget_t::on_run_clicked()
{
    QString goal = this->goal_edit->toPlainText().trimmed();
    if (goal.isEmpty())
    {
        return;
    }

    if (this->try_execute_slash_command(goal))
    {
        return;
    }

    const QString selected_model = this->model_combo->currentText().trimmed();
    const QString selected_mode = this->mode_combo->currentData().toString();

    // Auto-compact: if enabled and the previous run's input-token count exceeded the
    // threshold, run a compact pass first, then restart with the original goal.
    const auto &s = settings();
    const int prev_input_tokens = this->displayed_usage.input_tokens;
    const bool auto_compact_needed = s.auto_compact_enabled && !this->skip_auto_compact_once &&
                                     prev_input_tokens >= s.auto_compact_threshold_tokens &&
                                     prev_input_tokens >= 0 && this->deferred_run_goal.isEmpty();
    this->skip_auto_compact_once = false;

    if (auto_compact_needed)
    {
        this->deferred_run_goal = goal;
        goal = compact_prompt_text();
        emit this->controller->log_message(
            QStringLiteral("🗜 Auto-compact triggered (context: %1 tokens ≥ %2). "
                           "Compacting before running your request…")
                .arg(prev_input_tokens)
                .arg(s.auto_compact_threshold_tokens));
    }

    this->reset_usage_display(settings().provider, selected_model);
    this->last_committed_streaming_markdown.clear();
    this->controller->set_request_context(
        this->linked_files_controller->linked_files_prompt_context(),
        this->linked_files_controller->effective_linked_files());
    this->goal_edit->clear();

    this->update_run_state(true);
    this->tabs->setCurrentWidget(this->log_view);
    this->controller->start(goal, this->dry_run_check->isChecked(),
                            selected_mode == QStringLiteral("ask")
                                ? agent_controller_t::run_mode_t::ASK
                                : agent_controller_t::run_mode_t::AGENT,
                            selected_model, this->reasoning_combo->currentData().toString(),
                            this->thinking_combo->currentData().toString());
}

void agent_dock_widget_t::on_stop_clicked()
{
    this->deferred_run_goal.clear();
    this->controller->stop();
    this->update_run_state(false);
}

void agent_dock_widget_t::on_apply_patch_clicked()
{
    if (this->current_diff.isEmpty())
    {
        return;
    }

    auto *diff_preview = static_cast<diff_preview_edit_t *>(this->diff_view);
    if (!diff_preview->has_approved_changes())
    {
        QMessageBox::information(
            this, tr("Apply Patch"),
            tr("Approve at least one changed diff line in the gutter before applying."));
        return;
    }

    const QString diff_to_apply = diff_preview->approved_diff().trimmed();
    if (diff_to_apply.isEmpty())
    {
        QMessageBox::information(this, tr("Apply Patch"), tr("No approved changes to apply."));
        return;
    }

    const QString work_dir = this->session_controller->current_project_dir();

    if (work_dir.isEmpty())
    {
        QMessageBox::warning(this, tr("Apply Patch"), tr("No project directory found."));
        return;
    }

    QString error_msg;
    if (Diff::apply_patch(diff_to_apply, work_dir, false, error_msg))
    {
        this->applied_diff = diff_to_apply;
        this->revert_patch_btn->setEnabled(true);
        on_log_message(tr("✅ Patch applied successfully."));
    }
    else
    {
        QMessageBox::critical(this, tr("Apply Patch"), tr("Failed: %1").arg(error_msg));
    }
}

void agent_dock_widget_t::on_revert_patch_clicked()
{
    const QString diff_to_revert =
        this->applied_diff.isEmpty() ? this->current_diff : this->applied_diff;
    if (diff_to_revert.isEmpty())
    {
        return;
    }

    const QString work_dir = this->session_controller->current_project_dir();

    QString error_msg;
    if (Diff::revert_patch(diff_to_revert, work_dir, error_msg))
    {
        this->applied_diff.clear();
        this->revert_patch_btn->setEnabled(false);
        on_log_message(tr("↩ Patch reverted successfully."));
    }
    else
    {
        QMessageBox::critical(this, tr("Revert Patch"), tr("Failed: %1").arg(error_msg));
    }
}

void agent_dock_widget_t::on_copy_plan_clicked()
{
    QStringList items;
    for (int i = 0; i < this->plan_list->count(); ++i)
    {
        items.append(this->plan_list->item(i)->text());
    }
    QGuiApplication::clipboard()->setText(items.join('\n'));
}

void agent_dock_widget_t::on_log_message(const QString &msg)
{
    const QString text = msg.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    static const QString final_prefix = QStringLiteral("✅ Agent finished: ");
    const bool suppress_streamed_final_echo =
        text.startsWith(final_prefix) && text.mid(final_prefix.size()).trimmed() ==
                                             this->last_committed_streaming_markdown.trimmed();
    if (suppress_streamed_final_echo)
    {
        this->last_committed_streaming_markdown.clear();
        return;
    }

    this->append_stamped_log_entry(text);
}

void agent_dock_widget_t::on_provider_usage_available(const provider_usage_t &usage,
                                                      qint64 duration_ms)
{
    this->displayed_usage = accumulate_provider_usage(this->displayed_usage, usage);
    ++this->displayed_provider_request_count;
    this->update_usage_display();

    const QString body = provider_usage_markdown(usage, duration_ms);
    if (body.isEmpty())
    {
        return;
    }

    this->append_stamped_log_entry(body);
}

void agent_dock_widget_t::reset_usage_display(const QString &provider_id,
                                              const QString &model_name)
{
    this->usage_provider_id = provider_id.trimmed();
    this->usage_model_name = model_name.trimmed();
    this->displayed_usage = {};
    this->displayed_provider_request_count = 0;
    // Only blank the label when resetting for a full chat clear (no provider specified).
    // When called at run-start, keep showing the previous value until new data arrives.
    if (provider_id.isEmpty())
    {
        this->update_usage_display();
    }
}

void agent_dock_widget_t::update_usage_display()
{
    if (this->usage_value_label == nullptr)
    {
        return;
    }

    this->usage_value_label->setText(format_usage_value_for_provider(
        this->usage_provider_id, this->displayed_usage, this->displayed_provider_request_count));
}

void agent_dock_widget_t::append_stamped_log_entry(const QString &body)
{
    if (body.trimmed().isEmpty())
    {
        return;
    }

    this->flush_streaming_markdown();

    const QString stamped =
        QStringLiteral("`[%1]`  %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), body);

    if (!this->log_markdown.isEmpty())
    {
        this->log_markdown += QStringLiteral("\n\n");
    }
    this->log_markdown += stamped;
    this->session_controller->append_current_conversation_log_entry(stamped);

    this->render_throttle->stop();
    this->render_log_full();
}

void agent_dock_widget_t::flush_streaming_markdown()
{
    if (this->streaming_markdown.isEmpty() && this->streaming_response_raw.isEmpty())
    {
        return;
    }

    if (this->streaming_markdown.isEmpty() == false)
    {
        const QString flushed_markdown = redact_read_file_payloads(this->streaming_markdown);
        this->last_committed_streaming_markdown = flushed_markdown;
        if (!this->log_markdown.isEmpty())
        {
            this->log_markdown += QStringLiteral("\n\n");
        }
        this->log_markdown += flushed_markdown;
        this->session_controller->append_current_conversation_log_entry(flushed_markdown);
    }
    this->streaming_markdown.clear();
    this->streaming_response_raw.clear();
    this->streaming_rendered_len = 0;
    this->is_streaming = false;
}

void agent_dock_widget_t::render_log()
{
    if (this->is_streaming)
    {
        this->streaming_rendered_len = this->streaming_markdown.size();
        this->render_log_full();
        return;
    }

    this->render_log_full();
}

void agent_dock_widget_t::render_log_full()
{
    const QString text = this->current_log_markdown();
    const QString rendered_text = wrap_markdown_payload_blocks(text);

    this->log_view->setMarkdown(rendered_text);
    this->raw_markdown_view->setPlainText(text);

    // Defer scroll to allow document layout to complete
    QTimer::singleShot(0, this->log_view, [this]() {
        QTextCursor cursor = this->log_view->textCursor();
        cursor.movePosition(QTextCursor::End);
        this->log_view->setTextCursor(cursor);
        this->log_view->ensureCursorVisible();
    });
    QTimer::singleShot(0, this->raw_markdown_view, [this]() {
        QTextCursor cursor = this->raw_markdown_view->textCursor();
        cursor.movePosition(QTextCursor::End);
        this->raw_markdown_view->setTextCursor(cursor);
        this->raw_markdown_view->ensureCursorVisible();
    });
}

void agent_dock_widget_t::render_log_streaming(const QString &new_tokens)
{
    // Append new tokens as plain text at the end of the log view
    QTextCursor cursor(this->log_view->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(new_tokens);
    this->log_view->ensureCursorVisible();

    // Also append to raw markdown view
    QTextCursor rawCursor(this->raw_markdown_view->document());
    rawCursor.movePosition(QTextCursor::End);
    rawCursor.insertText(new_tokens);
    this->raw_markdown_view->ensureCursorVisible();
}

void agent_dock_widget_t::on_plan_updated(const QList<plan_step_t> &steps)
{
    this->plan_list->clear();
    for (const auto &step : steps)
    {
        this->plan_list->addItem(
            QStringLiteral("%1. %2").arg(step.index + 1).arg(step.description));
    }
    this->session_controller->save_chat();
    this->tabs->setCurrentWidget(this->plan_list);
}

void agent_dock_widget_t::on_diff_available(const QString &diff)
{
    this->sync_diff_ui(diff, true, true);
}

void agent_dock_widget_t::on_approval_requested(int id, const QString &action,
                                                const QString &reason, const QString &preview)
{
    auto *item = new QListWidgetItem(QStringLiteral("[#%1] %2 — %3").arg(id).arg(action, reason));
    item->setData(Qt::UserRole, id);
    item->setToolTip(preview);
    this->approval_list->addItem(item);
    this->approval_items[id] = item;
    this->tabs->setCurrentWidget(this->approval_list);

    // Show approval dialog
    auto result = QMessageBox::question(
        this, tr("Approval Required"),
        QStringLiteral("%1\n\nReason: %2\n\nPreview:\n%3").arg(action, reason, preview.left(1000)),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes)
    {
        item->setText(item->text() + QStringLiteral(" ✅"));
        this->controller->approve_action(id);
    }
    else
    {
        item->setText(item->text() + QStringLiteral(" ❌"));
        this->controller->deny_action(id);
    }
}

void agent_dock_widget_t::on_iteration_changed(int iteration)
{
    this->on_log_message(QStringLiteral("🔄 Iteration %1 / Tool calls: %2")
                             .arg(iteration)
                             .arg(this->controller->tool_call_count()));
}

void agent_dock_widget_t::on_stopped(const QString &summary)
{
    this->update_run_state(false);
    if (this->render_throttle->isActive())
    {
        this->render_throttle->stop();
    }
    if (!this->streaming_markdown.isEmpty() || !this->streaming_response_raw.isEmpty())
    {
        this->flush_streaming_markdown();
        this->render_log_full();
    }
    if (this->status_label->text().isEmpty() == true ||
        this->status_label->text() == tr("Ready") ||
        this->status_label->text() == tr("Running..."))
    {
        this->status_label->setText(QStringLiteral("Done: %1").arg(summary.left(80)));
    }
    this->session_controller->save_chat();

    // If a deferred goal was queued by auto-compact, launch it now.
    if (!this->deferred_run_goal.isEmpty())
    {
        const QString goal = this->deferred_run_goal;
        this->deferred_run_goal.clear();
        this->skip_auto_compact_once = true;
        QMetaObject::invokeMethod(
            this,
            [this, goal]() {
                this->goal_edit->setPlainText(goal);
                this->on_run_clicked();
            },
            Qt::QueuedConnection);
    }
}

bool agent_dock_widget_t::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == this->goal_edit && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);

        if (this->goal_edit->has_completion_popup())
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
            this->on_run_clicked();
            return true;
        }

        // Escape — stop agent
        if (ke->key() == Qt::Key_Escape)
        {
            this->on_stop_clicked();
            return true;
        }

        // Ctrl+L — clear log
        if (ke->key() == Qt::Key_L && ((ke->modifiers() & Qt::ControlModifier) != 0u))
        {
            this->log_view->clear();
            this->raw_markdown_view->clear();
            this->log_markdown.clear();
            this->streaming_markdown.clear();
            this->streaming_response_raw.clear();
            this->last_committed_streaming_markdown.clear();
            this->streaming_rendered_len = 0;
            this->is_streaming = false;
            this->session_controller->save_chat();
            return true;
        }
    }
    if (obj == this->linked_files_view && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace)
        {
            this->linked_files_controller->remove_selected_linked_files();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

}  // namespace qcai2
