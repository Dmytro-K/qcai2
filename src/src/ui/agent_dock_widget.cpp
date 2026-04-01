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
#include "actions_log_links.h"
#include "auto_hiding_list_widget.h"
#include "debugger_status_widget.h"
#include "decision_request_widget.h"
#include "diff_preview_navigation.h"
#include "image_attachment_preview_strip.h"
#include "request_duration_formatter.h"
#include "ui_agent_dock_widget.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QAbstractScrollArea>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextOption>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
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

QColor fallback_request_accent_color()
{
    return QColor(QStringLiteral("#2f7cf6"));
}

QString request_status_label_stylesheet(bool running)
{
    const QPalette palette = QGuiApplication::palette();
    if (running == false)
    {
        return QStringLiteral("font-weight: bold; color: %1; padding: 0px 2px;")
            .arg(palette.color(QPalette::Mid).name(QColor::HexRgb));
    }

    const bool is_dark = palette.color(QPalette::Base).lightness() < 128;
    const QColor text =
        is_dark ? QColor(QStringLiteral("#ff8fc7")) : QColor(QStringLiteral("#c2187a"));

    return QStringLiteral("font-weight: 700; color: %1;").arg(text.name(QColor::HexRgb));
}

QString request_duration_label_stylesheet(bool running)
{
    const QPalette palette = QGuiApplication::palette();
    if (running == false)
    {
        return QStringLiteral("color: %1; padding: 0px 2px;")
            .arg(palette.color(QPalette::Mid).name(QColor::HexRgb));
    }

    const QColor accent = palette.color(QPalette::Highlight).isValid() == true
                              ? palette.color(QPalette::Highlight)
                              : fallback_request_accent_color();
    const QColor text = accent.lightness() < 128 ? accent.lighter(150) : accent.darker(125);

    return QStringLiteral("font-weight: 600; color: %1; padding: 0px 2px;")
        .arg(text.name(QColor::HexRgb));
}

Utils::Id diff_editor_id_for_file_path(const Utils::FilePath &file_path)
{
    Q_UNUSED(file_path);
    return Utils::Id(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID);
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

constexpr int approval_preview_role = Qt::UserRole + 1;
constexpr int approval_pending_role = Qt::UserRole + 2;
constexpr int approval_inline_role = Qt::UserRole + 3;

struct inline_approval_diff_t
{
    QString full_diff;
    QString review_diff;
};

QString strip_modified_headers(const QString &diff)
{
    const QStringList lines = diff.split(QLatin1Char('\n'));
    QStringList clean_lines;
    clean_lines.reserve(lines.size());
    for (const QString &line : lines)
    {
        if (line.startsWith(QStringLiteral("=== MODIFIED:")) == false)
        {
            clean_lines.append(line);
        }
    }
    return clean_lines.join(QLatin1Char('\n'));
}

QString review_project_dir(agent_dock_session_controller_t *session_controller,
                           agent_controller_t *controller)
{
    if (session_controller != nullptr)
    {
        const QString session_project_dir = session_controller->current_project_dir().trimmed();
        if (session_project_dir.isEmpty() == false)
        {
            return session_project_dir;
        }
    }

    if (controller != nullptr && controller->editor_context() != nullptr)
    {
        return controller->editor_context()->capture().project_dir.trimmed();
    }

    return {};
}

inline_approval_diff_t extract_inline_approval_diff(const QString &action, const QString &preview,
                                                    const QString &project_dir)
{
    if (action != QStringLiteral("apply_patch") || project_dir.trimmed().isEmpty() == true)
    {
        return {};
    }

    QString full_diff;
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(preview.toUtf8(), &parse_error);
    if (parse_error.error == QJsonParseError::NoError && doc.isObject() == true)
    {
        full_diff = doc.object().value(QStringLiteral("diff")).toString();
    }
    else
    {
        full_diff = preview.trimmed();
    }

    if (full_diff.trimmed().isEmpty() == true)
    {
        return {};
    }

    const Diff::new_file_result_t extracted =
        Diff::extract_and_create_new_files(full_diff, project_dir, true);
    if (extracted.error.isEmpty() == false)
    {
        return {};
    }

    const QString review_diff =
        Diff::normalize(strip_modified_headers(extracted.remaining_diff)).trimmed();
    if (review_diff.isEmpty() == true)
    {
        return {};
    }

    return {full_diff, review_diff};
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

void show_fallback_image_dialog(QWidget *parent, const QString &title, const QPixmap &pixmap)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(900, 700);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *scroll_area = new QScrollArea(dialog);
    scroll_area->setWidgetResizable(true);
    auto *label = new QLabel(scroll_area);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(pixmap);
    scroll_area->setWidget(label);
    layout->addWidget(scroll_area);
    dialog->show();
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
        this->setMouseTracking(true);
        this->viewport()->setMouseTracking(true);
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
        this->set_diff_text(diff, QString());
    }

    void set_diff_text(const QString &diff, const QString &project_dir)
    {
        setPlainText(diff);
        this->navigation_targets = build_diff_preview_navigation_targets(diff, project_dir);
        this->recalculate_approvals();
        this->notify_approval_changed();
        this->line_number_area->update();
    }

    void set_approval_changed_callback(std::function<void(qsizetype, qsizetype)> callback)
    {
        this->approval_changed_callback = std::move(callback);
        this->notify_approval_changed();
    }

    void set_navigation_callback(std::function<void(const QString &, int)> callback)
    {
        this->navigation_callback = std::move(callback);
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
    void mouseMoveEvent(QMouseEvent *event) override
    {
        this->update_navigation_cursor(event->pos());
        QPlainTextEdit::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            const diff_preview_navigation_target_t target =
                this->navigation_target_for_position(event->pos());
            if (target.is_valid() == true &&
                static_cast<bool>(this->navigation_callback) == true &&
                this->is_position_over_navigable_text(event->pos()) == true)
            {
                this->navigation_callback(target.absolute_file_path, target.line);
                event->accept();
                return;
            }
        }

        QPlainTextEdit::mousePressEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        this->viewport()->unsetCursor();
        QPlainTextEdit::leaveEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        this->line_number_area->setGeometry(
            QRect(cr.left(), cr.top(), this->line_number_area_width(), cr.height()));
    }

private:
    bool is_position_over_navigable_text(const QPoint &position) const
    {
        const diff_preview_navigation_target_t target =
            this->navigation_target_for_position(position);
        if (target.is_valid() == false || static_cast<bool>(this->navigation_callback) == false)
        {
            return false;
        }

        const QTextCursor cursor = this->cursorForPosition(position);
        const QTextBlock block = cursor.block();
        if (block.isValid() == false || block.layout() == nullptr ||
            block.layout()->lineCount() == 0)
        {
            return false;
        }

        const QTextLine line = block.layout()->lineAt(0);
        const QRectF block_rect = blockBoundingGeometry(block).translated(contentOffset());
        const qreal line_top = block_rect.top() + line.y();
        const qreal line_bottom = line_top + line.height();
        if (position.y() < line_top || position.y() >= line_bottom)
        {
            return false;
        }

        const qreal line_left = block_rect.left() + line.x();
        const qreal line_right = line_left + line.naturalTextWidth();
        return position.x() >= line_left && position.x() <= line_right;
    }

    diff_preview_navigation_target_t navigation_target_for_position(const QPoint &position) const
    {
        const QTextCursor cursor = this->cursorForPosition(position);
        const int preview_line = cursor.block().blockNumber() + 1;
        return this->navigation_targets.value(preview_line);
    }

    void update_navigation_cursor(const QPoint &position)
    {
        if (this->is_position_over_navigable_text(position) == true)
        {
            this->viewport()->setCursor(Qt::PointingHandCursor);
            return;
        }

        this->viewport()->unsetCursor();
    }

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
    QHash<int, diff_preview_navigation_target_t> navigation_targets;
    std::function<void(const QString &, int)> navigation_callback;
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
                                         i_debugger_session_service_t *debugger_service,
                                         QWidget *parent)
    : QWidget(parent), controller(controller), debugger_service(debugger_service),
      linked_files_controller(std::make_unique<agent_dock_linked_files_controller_t>(*this)),
      session_controller(std::make_unique<agent_dock_session_controller_t>(
          *this, chat_context_manager, debugger_service))
{
    this->setup_ui();

    this->inline_diff_manager = new inline_diff_manager_t(this);

    // Throttle rendering during streaming to avoid O(n²) setPlainText() calls
    this->render_throttle = new QTimer(this);
    this->render_throttle->setSingleShot(true);
    this->render_throttle->setInterval(50);
    connect(this->render_throttle, &QTimer::timeout, this, &agent_dock_widget_t::render_log);
    this->request_duration_timer = new QTimer(this);
    this->request_duration_timer->setInterval(1000);
    connect(this->request_duration_timer, &QTimer::timeout, this,
            &agent_dock_widget_t::update_request_duration_display);

    // Connect controller signals
    connect(this->controller, &agent_controller_t::log_message, this,
            &agent_dock_widget_t::on_log_message);
    connect(this->controller, &agent_controller_t::provider_usage_available, this,
            &agent_dock_widget_t::on_provider_usage_available);
    connect(this->controller, &agent_controller_t::status_changed, this,
            [this](const QString &status) { this->status_label->setText(status); });
    connect(this->controller, &agent_controller_t::soft_steer_pending, this,
            [this](int queued_message_count) {
                this->status_label->setText(
                    QStringLiteral("Soft steer pending (%1 queued).").arg(queued_message_count));
            });
    connect(this->controller, &agent_controller_t::soft_steer_applied, this,
            [this](int applied_message_count) {
                this->discard_streaming_markdown();
                this->append_stamped_log_entry(
                    QStringLiteral("↪ Soft steer applied with %1 follow-up message%2.")
                        .arg(applied_message_count)
                        .arg(applied_message_count == 1 ? QString() : QStringLiteral("s")));
                this->status_label->setText(QStringLiteral("Soft steer applied."));
            });
    connect(this->controller, &agent_controller_t::streaming_token, this,
            [this](const QString &token) {
                this->streaming_response_raw += token;
                this->streaming_markdown =
                    streaming_response_markdown_preview(this->streaming_response_raw);
                if (!this->is_streaming)
                {
                    this->is_streaming = true;
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
    connect(this->controller, &agent_controller_t::decision_requested, this,
            &agent_dock_widget_t::on_decision_requested);
    connect(this->controller, &agent_controller_t::decision_answered, this,
            [this](const QString &request_id, const QString &answer_summary) {
                if (this->decision_request_widget != nullptr &&
                    this->decision_request_widget->current_request_id() == request_id)
                {
                    this->decision_request_widget->mark_resolved(answer_summary);
                }
                this->status_label->setText(QStringLiteral("Decision answered."));
                this->session_controller->save_chat();
            });
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
        const QString accepted_diff = this->inline_diff_manager->accepted_diff();
        const QString rejected_diff = this->inline_diff_manager->rejected_diff();
        const bool needs_refinement = this->inline_diff_manager->rejected_count() > 0;
        QTimer::singleShot(0, this, [this, accepted_diff, rejected_diff, needs_refinement]() {
            if (this->active_inline_approval_id >= 0)
            {
                const int approval_id = this->active_inline_approval_id;
                this->active_inline_approval_id = -1;

                QListWidgetItem *item = this->approval_items.value(approval_id, nullptr);
                const bool resolved = this->controller->resolve_apply_patch_inline_approval(
                    approval_id, accepted_diff, rejected_diff);
                if (resolved == true && item != nullptr)
                {
                    item->setData(approval_pending_role, false);
                    item->setText(item->text() + (accepted_diff.trimmed().isEmpty() == true
                                                      ? QStringLiteral(" ❌")
                                                      : (rejected_diff.trimmed().isEmpty() == true
                                                             ? QStringLiteral(" ✅")
                                                             : QStringLiteral(" ↩️"))));
                    this->approval_items.remove(approval_id);
                    this->update_approval_selection_ui();
                    this->session_controller->save_chat();
                    return;
                }
            }

            if (needs_refinement == true)
            {
                this->controller->request_inline_diff_refinement(accepted_diff, rejected_diff);
                return;
            }
            this->controller->finalize_inline_diff_review();
        });
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

    connect(&settings_change_notifier(), &settings_change_notifier_t::settings_changed, this,
            [this]() { this->linked_files_controller->refresh_ui(); });

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

void agent_dock_widget_t::queue_current_request()
{
    this->on_queue_clicked();
}

QString agent_dock_widget_t::current_log_markdown() const
{
    return this->current_log_markdown_blocks().join(QStringLiteral("\n\n"));
}

QStringList agent_dock_widget_t::current_log_markdown_blocks() const
{
    QStringList blocks;
    blocks.reserve(this->log_markdown_blocks.size() +
                   (this->streaming_markdown.isEmpty() ? 0 : 1));
    for (const QString &block : std::as_const(this->log_markdown_blocks))
    {
        blocks.append(redact_read_file_payloads(block));
    }
    if (this->streaming_markdown.isEmpty() == false)
    {
        blocks.append(redact_read_file_payloads(this->streaming_markdown));
    }
    return blocks;
}

void agent_dock_widget_t::sync_raw_markdown_view()
{
    this->raw_markdown_view->setPlainText(this->current_log_markdown());
}

void agent_dock_widget_t::append_raw_markdown_block(const QString &markdown_block)
{
    if (markdown_block.trimmed().isEmpty() == true)
    {
        return;
    }

    QTextCursor cursor(this->raw_markdown_view->document());
    cursor.movePosition(QTextCursor::End);
    if (this->raw_markdown_view->document()->isEmpty() == false)
    {
        cursor.insertText(QStringLiteral("\n\n"));
    }
    cursor.insertText(markdown_block);
}

void agent_dock_widget_t::scroll_log_views_to_bottom()
{
    if (this->log_view != nullptr && this->actions_log_model != nullptr &&
        this->actions_log_model->rowCount() > 0)
    {
        this->log_view->scrollToBottom();
    }

    QTextCursor cursor(this->raw_markdown_view->document());
    cursor.movePosition(QTextCursor::End);
    this->raw_markdown_view->setTextCursor(cursor);
    this->raw_markdown_view->ensureCursorVisible();
}

void agent_dock_widget_t::set_actions_log_active_row(const QModelIndex &current,
                                                     const QModelIndex &previous)
{
    if (this->log_view == nullptr)
    {
        return;
    }

    if (previous.isValid() == true)
    {
        this->log_view->closePersistentEditor(previous);
    }

    if (current.isValid() == false)
    {
        this->actions_log_active_row = -1;
        return;
    }

    this->actions_log_active_row = current.row();
    this->log_view->openPersistentEditor(current);
}

void agent_dock_widget_t::restore_actions_log_active_row()
{
    if (this->log_view == nullptr || this->actions_log_model == nullptr)
    {
        return;
    }

    const int row_count = this->actions_log_model->rowCount();
    if (row_count <= 0)
    {
        this->actions_log_active_row = -1;
        return;
    }

    const int target_row = std::clamp(
        this->actions_log_active_row >= 0 ? this->actions_log_active_row : row_count - 1, 0,
        row_count - 1);
    const QModelIndex target_index = this->actions_log_model->index(target_row, 0);
    if (target_index.isValid() == false)
    {
        return;
    }

    if (this->log_view->currentIndex() != target_index)
    {
        this->log_view->setCurrentIndex(target_index);
        return;
    }

    this->actions_log_active_row = target_row;
    this->log_view->openPersistentEditor(target_index);
}

void agent_dock_widget_t::sync_diff_ui(const QString &diff, bool focusDiffTab,
                                       bool refreshInlineMarkers)
{
    this->current_diff = diff;
    this->applied_diff.clear();
    this->revert_patch_btn->setEnabled(false);
    const QString work_dir = review_project_dir(this->session_controller.get(), this->controller);

    auto *diff_preview = static_cast<diff_preview_edit_t *>(this->diff_view);
    diff_preview->set_diff_text(diff, work_dir);
    this->apply_patch_btn->setEnabled(!diff.isEmpty() && diff_preview->has_approved_changes());

    if (focusDiffTab)
    {
        if (auto *page = this->diff_view->parentWidget())
        {
            this->tabs->setCurrentWidget(page);
        }
    }

    this->diff_file_list->clear();

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
            const Utils::FilePath file_path = Utils::FilePath::fromString(absPath);
            Core::EditorManager::openEditorAt(Utils::Link(file_path, 0, 0),
                                              diff_editor_id_for_file_path(file_path));
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
    this->image_attachments.clear();
    this->actions_log_model->clear();
    this->raw_markdown_view->clear();
    this->log_markdown.clear();
    this->log_markdown_blocks.clear();
    this->streaming_markdown.clear();
    this->streaming_response_raw.clear();
    this->last_committed_streaming_markdown.clear();
    this->is_streaming = false;
    this->actions_log_active_row = -1;
    this->plan_list->clear();
    this->inline_diff_manager->clear_all();
    static_cast<diff_preview_edit_t *>(this->diff_view)->set_diff_text(QString());
    this->diff_file_list->clear();
    this->approval_list->clear();
    this->approval_items.clear();
    this->approval_preview_view->clear();
    this->active_inline_approval_id = -1;
    if (this->decision_request_widget != nullptr)
    {
        this->decision_request_widget->clear_request();
    }
    this->applied_diff.clear();
    this->current_diff.clear();
    this->status_label->setText(tr("Ready"));
    this->stop_request_duration_display();
    this->deferred_request = queued_request_t{};
    this->has_deferred_request = false;
    this->skip_auto_compact_once = false;
    this->request_queue.clear();
    this->refresh_pending_items_view();
    this->reset_usage_display();
    this->update_approval_selection_ui();
    this->apply_patch_btn->setEnabled(false);
    this->revert_patch_btn->setEnabled(false);
    this->tabs->setCurrentWidget(this->plan_page);
    this->linked_files_controller->refresh_ui();
    this->refresh_image_attachment_ui();
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

void agent_dock_widget_t::import_image_attachment_from_image(const QImage &image)
{
    image_attachment_t attachment;
    QString error;
    if (this->session_controller->import_image_attachment_from_image(image, &attachment, &error) ==
        false)
    {
        QMessageBox::warning(this, tr("Image Attachment"),
                             error.isEmpty() ? tr("Failed to attach pasted image.") : error);
        return;
    }

    this->image_attachments.append(attachment);
    this->refresh_image_attachment_ui();
    this->session_controller->save_chat();
}

void agent_dock_widget_t::import_image_attachments_from_paths(const QStringList &paths)
{
    QStringList non_image_paths;
    for (const QString &path : paths)
    {
        image_attachment_t attachment;
        QString error;
        if (this->session_controller->import_image_attachment_from_file(path, &attachment,
                                                                        &error) == true)
        {
            this->image_attachments.append(attachment);
            continue;
        }

        if (error.contains(QStringLiteral("not a readable image"), Qt::CaseInsensitive) == true)
        {
            non_image_paths.append(path);
            continue;
        }

        QMessageBox::warning(this, tr("Image Attachment"),
                             error.isEmpty() ? tr("Failed to attach dropped image.") : error);
    }

    if (non_image_paths.isEmpty() == false)
    {
        this->linked_files_controller->add_linked_files(non_image_paths);
    }

    this->refresh_image_attachment_ui();
    this->session_controller->save_chat();
}

void agent_dock_widget_t::remove_image_attachment(const QString &attachment_id)
{
    for (int index = 0; index < this->image_attachments.size(); ++index)
    {
        if (this->image_attachments.at(index).attachment_id != attachment_id)
        {
            continue;
        }

        QString error;
        if (this->session_controller->remove_image_attachment_file(
                this->image_attachments.at(index), &error) == false)
        {
            QMessageBox::warning(this, tr("Image Attachment"),
                                 error.isEmpty() ? tr("Failed to remove image attachment.")
                                                 : error);
            return;
        }

        this->image_attachments.removeAt(index);
        this->refresh_image_attachment_ui();
        this->session_controller->save_chat();
        return;
    }
}

void agent_dock_widget_t::open_image_attachment(const QString &attachment_id)
{
    for (const image_attachment_t &attachment : std::as_const(this->image_attachments))
    {
        if (attachment.attachment_id != attachment_id)
        {
            continue;
        }

        const QString absolute_path =
            this->session_controller->absolute_path_for_image_attachment(attachment);
        if (absolute_path.isEmpty() == true)
        {
            QMessageBox::warning(this, tr("Image Attachment"),
                                 tr("The selected image attachment path is invalid."));
            return;
        }

        if (QDesktopServices::openUrl(QUrl::fromLocalFile(absolute_path)) == true)
        {
            return;
        }

        const QPixmap pixmap(absolute_path);
        if (pixmap.isNull() == true)
        {
            QMessageBox::warning(this, tr("Image Attachment"),
                                 tr("Failed to open the selected image attachment."));
            return;
        }

        show_fallback_image_dialog(this,
                                   attachment.file_name.trimmed().isEmpty() == true
                                       ? tr("Image Attachment")
                                       : attachment.file_name,
                                   pixmap);
        return;
    }
}

void agent_dock_widget_t::refresh_image_attachment_ui()
{
    if (this->image_attachment_strip == nullptr)
    {
        return;
    }

    this->image_attachment_strip->set_attachments(this->image_attachments,
                                                  this->session_controller->current_project_dir());
}

QList<image_attachment_t> agent_dock_widget_t::resolve_request_image_attachments(
    const QList<image_attachment_t> &attachments, QString *error) const
{
    QList<image_attachment_t> resolved;
    resolved.reserve(attachments.size());
    for (const image_attachment_t &attachment : attachments)
    {
        image_attachment_t resolved_attachment = attachment;
        resolved_attachment.storage_path =
            this->session_controller->absolute_path_for_image_attachment(attachment);
        if (resolved_attachment.storage_path.isEmpty() == true ||
            QFileInfo::exists(resolved_attachment.storage_path) == false)
        {
            if (error != nullptr)
            {
                *error = tr("Image attachment is missing: %1")
                             .arg(attachment.file_name.trimmed().isEmpty() == true
                                      ? attachment.storage_path
                                      : attachment.file_name);
            }
            return {};
        }
        resolved.append(resolved_attachment);
    }
    return resolved;
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
                                 "messages, local state, and stored image attachments."),
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
    this->plan_page = this->ui->planPage;
    this->actions_log_page = this->ui->actionsLogPage;
    this->plan_list = this->ui->planList;
    this->log_view = this->ui->logView;
    this->raw_markdown_view = this->ui->rawMarkdownView;
    this->diff_file_list = this->ui->diffFileList;
    this->approval_list = this->ui->approvalList;
    this->approval_preview_view = this->ui->approvalPreviewView;
    this->debug_log_view = this->ui->debugLogView;
    this->apply_patch_btn = this->ui->applyPatchBtn;
    this->revert_patch_btn = this->ui->revertPatchBtn;
    this->copy_plan_btn = this->ui->copyPlanBtn;
    this->approve_action_btn = this->ui->approveActionBtn;
    this->deny_action_btn = this->ui->denyActionBtn;
    this->rename_conversation_btn = this->ui->renameConversationBtn;
    this->delete_conversation_btn = this->ui->deleteConversationBtn;
    auto *clear_debug_btn = this->ui->clearDebugBtn;
    auto *new_chat_btn = this->ui->newChatButton;
    this->queue_btn = new QPushButton(tr("Queue"), this);
    this->queue_btn->setObjectName(QStringLiteral("queueBtn"));
    this->request_duration_label = new QLabel(this);
    this->request_duration_label->setObjectName(QStringLiteral("requestDurationLabel"));
    this->request_duration_label->setAlignment(Qt::AlignHCenter);
    this->request_duration_label->setVisible(false);
    this->request_duration_label->setText(format_request_duration_label(0));
    this->decision_request_widget = new decision_request_widget_t(this);
    this->decision_request_widget->setObjectName(QStringLiteral("decisionRequestWidget"));
    this->pending_items_view = new auto_hiding_list_widget_t(this);
    this->pending_items_view->setObjectName(QStringLiteral("pendingItemsView"));
    this->pending_items_view->setSelectionMode(QAbstractItemView::NoSelection);
    this->pending_items_view->setFocusPolicy(Qt::NoFocus);
    this->pending_items_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->pending_items_view->setWordWrap(true);
    this->pending_items_view->setUniformItemSizes(false);
    this->pending_items_view->setAlternatingRowColors(true);
    this->pending_items_view->setToolTip(
        tr("Pending items that will run automatically in FIFO order."));
    this->pending_items_view->setMaximumHeight(this->fontMetrics().height() * 5);
    this->ui->inputPanelLayout->insertWidget(0, this->decision_request_widget);
    this->ui->inputPanelLayout->insertWidget(1, this->pending_items_view);
    this->ui->btnColumn->insertWidget(this->ui->btnColumn->indexOf(this->run_btn) + 1,
                                      this->queue_btn);
    this->ui->btnColumn->insertWidget(this->ui->btnColumn->indexOf(clear_debug_btn) + 1,
                                      this->request_duration_label);
    this->update_request_status_visuals(false);
    connect(this->decision_request_widget, &decision_request_widget_t::option_submitted, this,
            &agent_dock_widget_t::submit_decision_option);
    connect(this->decision_request_widget, &decision_request_widget_t::freeform_submitted, this,
            &agent_dock_widget_t::submit_decision_freeform);

    this->ui->contentSplitter->setStretchFactor(0, 3);
    this->ui->contentSplitter->setStretchFactor(1, 1);
    this->ui->inputRow->setStretch(0, 1);
    this->ui->modeModelRow->setStretch(2, 1);
    this->reset_usage_display();

    this->linked_files_view = new linked_files_list_widget_t(this);
    this->linked_files_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->linked_files_view->setVisible(false);
    this->linked_files_view->setContextMenuPolicy(Qt::ActionsContextMenu);
    this->image_attachment_strip = new image_attachment_preview_strip_t(this);
    this->image_attachment_strip->setObjectName(QStringLiteral("imageAttachmentStrip"));
    auto *removeLinkedFileAction = new QAction(tr("Remove Linked File"), this->linked_files_view);
    connect(removeLinkedFileAction, &QAction::triggered, this,
            [this]() { this->linked_files_controller->remove_selected_linked_files(); });
    this->linked_files_view->addAction(removeLinkedFileAction);
    this->ui->goalColumn->insertWidget(0, this->linked_files_view);
    this->ui->goalColumn->insertWidget(1, this->image_attachment_strip);

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
    connect(
        this->goal_edit, &goal_text_edit_t::files_dropped, this,
        [this](const QStringList &paths) { this->import_image_attachments_from_paths(paths); });
    connect(this->goal_edit, &goal_text_edit_t::image_received, this,
            &agent_dock_widget_t::import_image_attachment_from_image);
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
    connect(this->image_attachment_strip, &image_attachment_preview_strip_t::remove_requested,
            this, &agent_dock_widget_t::remove_image_attachment);
    connect(this->image_attachment_strip, &image_attachment_preview_strip_t::open_requested, this,
            &agent_dock_widget_t::open_image_attachment);
    this->linked_files_view->installEventFilter(this);
    this->linked_files_controller->refresh_ui();
    this->refresh_image_attachment_ui();

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
    diff_preview->set_navigation_callback([](const QString &absolute_file_path, int line) {
        const Utils::FilePath file_path = Utils::FilePath::fromString(absolute_file_path);
        Core::EditorManager::openEditorAt(Utils::Link(file_path, qMax(1, line), 0),
                                          diff_editor_id_for_file_path(file_path));
    });

    this->actions_log_model = new actions_log_model_t(this);
    this->actions_log_delegate = new actions_log_delegate_t(this->log_view);
    this->actions_log_delegate->set_link_activated_handler(
        [this](const QString &href) { this->open_actions_log_link(href); });
    this->log_view->setModel(this->actions_log_model);
    this->log_view->setItemDelegate(this->actions_log_delegate);
    this->log_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->log_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    this->log_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->log_view->setSpacing(2);
    this->log_view->setUniformItemSizes(false);
    this->log_view->setWordWrap(true);
    this->log_view->setResizeMode(QListView::Adjust);
    this->log_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->log_view->setStyleSheet(QStringLiteral("QListView { padding: 4px; }"));
    connect(this->log_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &agent_dock_widget_t::set_actions_log_active_row);
    connect(this->actions_log_model, &QAbstractItemModel::modelReset, this,
            &agent_dock_widget_t::restore_actions_log_active_row);
    connect(this->actions_log_model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { this->restore_actions_log_active_row(); });
    connect(this->actions_log_model, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { this->restore_actions_log_active_row(); });
    this->raw_markdown_view->setFont(QFont(QStringLiteral("monospace"), 9));
    this->approval_preview_view->setFont(QFont(QStringLiteral("monospace"), 9));
    this->debug_log_view->setFont(QFont(QStringLiteral("monospace"), 9));
    this->debugger_status_widget = new debugger_status_widget_t(this->debugger_service, this);
    this->tabs->addTab(this->debugger_status_widget, tr("Debugger"));

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
    connect(
        this->approval_list, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem *, QListWidgetItem *) { this->update_approval_selection_ui(); });
    connect(this->approve_action_btn, &QPushButton::clicked, this,
            [this]() { this->resolve_selected_approval(true); });
    connect(this->deny_action_btn, &QPushButton::clicked, this,
            [this]() { this->resolve_selected_approval(false); });
    connect(clear_debug_btn, &QPushButton::clicked, this,
            &agent_dock_widget_t::on_clear_debug_clicked);
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
    connect(this->queue_btn, &QPushButton::clicked, this, &agent_dock_widget_t::on_queue_clicked);
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
    auto add_copy_shortcut_list = [this](QListView *view) {
        auto *copy_shortcut = new QShortcut(QKeySequence::Copy, view);
        copy_shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(copy_shortcut, &QShortcut::activated, view, [this, view]() {
            if (this->actions_log_model == nullptr || view->selectionModel() == nullptr)
            {
                return;
            }

            const QString selected_markdown = this->actions_log_model->selected_raw_markdown(
                view->selectionModel()->selectedRows());
            if (selected_markdown.isEmpty() == true)
            {
                return;
            }

            QGuiApplication::clipboard()->setText(selected_markdown);
        });
        auto *select_all_shortcut = new QShortcut(QKeySequence::SelectAll, view);
        select_all_shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(select_all_shortcut, &QShortcut::activated, view, &QListView::selectAll);
    };
    add_copy_shortcut_list(this->log_view);
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
    this->refresh_pending_items_view();
}

void agent_dock_widget_t::update_run_state(bool running)
{
    this->run_btn->setEnabled(true);
    this->run_btn->setText(running ? tr("Steer") : tr("Run"));
    if (this->queue_btn != nullptr)
    {
        this->queue_btn->setEnabled(true);
    }
    this->stop_btn->setEnabled(running);
    this->goal_edit->setReadOnly(false);
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

    if (running == true)
    {
        this->start_request_duration_display();
    }
    else
    {
        this->stop_request_duration_display();
    }

    this->update_request_status_visuals(running);
}

void agent_dock_widget_t::update_request_status_visuals(bool running)
{
    if (this->status_label != nullptr)
    {
        this->status_label->setStyleSheet(request_status_label_stylesheet(running));
    }

    if (this->request_duration_label != nullptr)
    {
        this->request_duration_label->setStyleSheet(request_duration_label_stylesheet(running));
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
        if (result.command_name == QStringLiteral("compact"))
        {
            if (this->controller->is_running() == true)
            {
                this->tabs->setCurrentWidget(this->actions_log_page);
                this->on_log_message(tr("Stop the current run before starting compaction."));
                return true;
            }

            this->start_compaction_request(result.goal_override,
                                           this->capture_current_request({}));
            return true;
        }
        goal = result.goal_override;
        return false;
    }

    this->tabs->setCurrentWidget(this->actions_log_page);

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

queued_request_t agent_dock_widget_t::capture_current_request(const QString &goal) const
{
    queued_request_t request;
    request.goal = goal.trimmed();
    request.request_context = this->linked_files_controller->linked_files_prompt_context();
    request.linked_files = this->linked_files_controller->effective_linked_files();
    request.attachments = this->image_attachments;
    request.dry_run = this->dry_run_check->isChecked();
    request.run_mode = this->mode_combo->currentData().toString() == QStringLiteral("ask")
                           ? agent_controller_t::run_mode_t::ASK
                           : agent_controller_t::run_mode_t::AGENT;
    request.model_name = this->model_combo->currentText().trimmed();
    request.reasoning_effort = this->reasoning_combo->currentData().toString();
    request.thinking_level = this->thinking_combo->currentData().toString();
    return request;
}

void agent_dock_widget_t::start_compaction_request(const QString &compact_goal,
                                                   const queued_request_t &base_request)
{
    queued_request_t compaction_request = base_request;
    compaction_request.goal = compact_goal.trimmed();
    compaction_request.request_context.clear();
    compaction_request.linked_files.clear();
    compaction_request.attachments.clear();
    compaction_request.run_mode = agent_controller_t::run_mode_t::AGENT;
    this->start_request(compaction_request, agent_controller_t::run_options_t{true});
}

void agent_dock_widget_t::start_request(const queued_request_t &request,
                                        const agent_controller_t::run_options_t &options)
{
    if (request.is_valid() == false)
    {
        return;
    }

    queued_request_t request_to_start = request;
    QString image_error;
    const QList<image_attachment_t> resolved_image_attachments =
        this->resolve_request_image_attachments(request_to_start.attachments, &image_error);
    if (image_error.isEmpty() == false)
    {
        QMessageBox::warning(this, tr("Image Attachment"), image_error);
        return;
    }

    const auto &s = settings();
    const int prev_input_tokens = this->displayed_usage.input_tokens;
    const bool auto_compact_needed = options.is_compaction == false && s.auto_compact_enabled &&
                                     !this->skip_auto_compact_once &&
                                     prev_input_tokens >= s.auto_compact_threshold_tokens &&
                                     prev_input_tokens >= 0 && this->has_deferred_request == false;
    this->skip_auto_compact_once = false;

    if (auto_compact_needed)
    {
        this->deferred_request = request;
        this->has_deferred_request = true;
        this->start_compaction_request(compact_prompt_text(), request);
        return;
    }

    this->reset_usage_display(settings().provider, request.model_name);
    this->last_committed_streaming_markdown.clear();
    this->controller->set_request_context(request_to_start.request_context,
                                          request_to_start.linked_files,
                                          resolved_image_attachments);
    this->goal_edit->clear();
    this->linked_files_controller->clear_state();
    this->linked_files_controller->refresh_ui();
    this->image_attachments.clear();
    this->refresh_image_attachment_ui();

    this->update_run_state(true);
    this->tabs->setCurrentWidget(this->actions_log_page);
    this->controller->start(request_to_start.goal, request_to_start.dry_run,
                            request_to_start.run_mode, request_to_start.model_name,
                            request_to_start.reasoning_effort, request_to_start.thinking_level,
                            options);
}

bool agent_dock_widget_t::enqueue_request(const queued_request_t &request)
{
    if (this->request_queue.enqueue(request) == false)
    {
        return false;
    }

    this->refresh_pending_items_view();
    return true;
}

void agent_dock_widget_t::maybe_start_next_queued_request()
{
    if (this->controller->is_running() == true)
    {
        return;
    }

    queued_request_t next_request;
    if (this->request_queue.take_next(&next_request) == false)
    {
        this->refresh_pending_items_view();
        return;
    }

    this->refresh_pending_items_view();
    this->start_request(next_request);
}

void agent_dock_widget_t::refresh_pending_items_view()
{
    if (this->pending_items_view == nullptr)
    {
        return;
    }

    this->pending_items_view->clear();
    for (const queued_request_t &request : this->request_queue.items())
    {
        auto *item = new QListWidgetItem(request.display_text(), this->pending_items_view);
        item->setToolTip(request.goal);
    }
    this->pending_items_view->refresh_visibility();
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

    if (this->controller->is_running() == true)
    {
        QString image_error;
        const QList<image_attachment_t> resolved_image_attachments =
            this->resolve_request_image_attachments(this->image_attachments, &image_error);
        if (image_error.isEmpty() == false)
        {
            QMessageBox::warning(this, tr("Image Attachment"), image_error);
            return;
        }
        this->controller->set_request_context(
            this->linked_files_controller->linked_files_prompt_context(),
            this->linked_files_controller->effective_linked_files(), resolved_image_attachments);
        if (this->controller->enqueue_soft_steer_message(
                goal, this->linked_files_controller->linked_files_prompt_context(),
                this->linked_files_controller->effective_linked_files(),
                resolved_image_attachments) == true)
        {
            this->goal_edit->clear();
            this->image_attachments.clear();
            this->refresh_image_attachment_ui();
            this->tabs->setCurrentWidget(this->actions_log_page);
        }
        return;
    }

    this->start_request(this->capture_current_request(goal));
}

void agent_dock_widget_t::on_stop_clicked()
{
    this->deferred_request = queued_request_t{};
    this->has_deferred_request = false;
    this->controller->stop();
    this->update_run_state(false);
}

void agent_dock_widget_t::on_queue_clicked()
{
    const QString goal = this->goal_edit->toPlainText().trimmed();
    if (goal.isEmpty() == true)
    {
        return;
    }

    if (goal.startsWith(QLatin1Char('/')) == true)
    {
        this->tabs->setCurrentWidget(this->actions_log_page);
        this->on_log_message(
            tr("Queued requests do not support slash commands. Use Run instead."));
        return;
    }

    const queued_request_t request = this->capture_current_request(goal);
    if (this->enqueue_request(request) == false)
    {
        return;
    }

    this->goal_edit->clear();
    this->linked_files_controller->clear_state();
    this->linked_files_controller->refresh_ui();
    this->tabs->setCurrentWidget(this->actions_log_page);
    this->status_label->setText(tr("Queued %1 request(s)").arg(this->request_queue.size()));
    this->on_log_message(QStringLiteral("📥 Queued request: %1").arg(request.display_text()));
    this->session_controller->save_chat();

    if (this->controller->is_running() == false && this->has_deferred_request == false)
    {
        this->maybe_start_next_queued_request();
    }
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

    const QString work_dir = review_project_dir(this->session_controller.get(), this->controller);

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

    const QString work_dir = review_project_dir(this->session_controller.get(), this->controller);

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

void agent_dock_widget_t::on_clear_debug_clicked()
{
    logger_t::instance().clear();
    this->debug_log_view->clear();
}

void agent_dock_widget_t::on_log_message(const QString &msg)
{
    const QString text = msg.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    static const QString final_prefix = QStringLiteral("✅ Agent finished: ");
    static const QString proposal_prefix = QStringLiteral("📝 Agent proposed changes: ");
    const auto matches_streamed_summary = [this, &text](const QString &prefix) {
        return text.startsWith(prefix) && text.mid(prefix.size()).trimmed() ==
                                              this->last_committed_streaming_markdown.trimmed();
    };
    const bool suppress_streamed_final_echo =
        matches_streamed_summary(final_prefix) || matches_streamed_summary(proposal_prefix);
    if (suppress_streamed_final_echo)
    {
        this->last_committed_streaming_markdown.clear();
        return;
    }

    this->append_stamped_log_entry(text);
    if (this->debugger_status_widget != nullptr)
    {
        this->debugger_status_widget->refresh();
    }
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

void agent_dock_widget_t::start_request_duration_display()
{
    if (this->request_duration_label == nullptr || this->request_duration_timer == nullptr)
    {
        return;
    }

    this->request_elapsed_timer.restart();
    this->request_duration_label->setText(format_request_duration_label(0));
    this->request_duration_label->setVisible(true);
    if (this->request_duration_timer->isActive() == false)
    {
        this->request_duration_timer->start();
    }
}

void agent_dock_widget_t::stop_request_duration_display()
{
    if (this->request_duration_timer != nullptr)
    {
        this->request_duration_timer->stop();
    }
    this->request_elapsed_timer.invalidate();
    if (this->request_duration_label != nullptr)
    {
        this->request_duration_label->clear();
        this->request_duration_label->setVisible(false);
    }
}

void agent_dock_widget_t::update_request_duration_display()
{
    if (this->request_duration_label == nullptr)
    {
        return;
    }
    if (this->request_elapsed_timer.isValid() == false)
    {
        this->request_duration_label->clear();
        this->request_duration_label->setVisible(false);
        return;
    }

    this->request_duration_label->setText(
        format_request_duration_label(this->request_elapsed_timer.elapsed()));
    this->request_duration_label->setVisible(true);
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
    this->log_markdown_blocks.append(stamped);
    this->session_controller->append_current_conversation_log_entry(stamped);

    this->render_throttle->stop();
    const QString visible_stamped = redact_read_file_payloads(stamped);
    this->actions_log_model->append_committed_entry(
        visible_stamped, this->render_actions_log_markdown(visible_stamped));
    this->append_raw_markdown_block(visible_stamped);
    this->scroll_log_views_to_bottom();
}

QString agent_dock_widget_t::render_actions_log_markdown(const QString &markdown) const
{
    return linkify_actions_log_file_references(wrap_markdown_payload_blocks(markdown));
}

void agent_dock_widget_t::open_actions_log_link(const QString &href)
{
    const std::optional<actions_log_file_link_t> link = parse_actions_log_file_link(QUrl(href));
    if (link.has_value() == false)
    {
        return;
    }

    const QString absolute_path =
        resolve_actions_log_file_link_path(*link, this->session_controller->current_project_dir());
    if (absolute_path.isEmpty() == true)
    {
        QMessageBox::warning(
            this, tr("Unable to open file"),
            tr("Could not resolve '%1' to an existing file in the current project context.")
                .arg(link->path));
        return;
    }

    Core::EditorManager::openEditorAt(
        Utils::Link(Utils::FilePath::fromString(absolute_path), link->line, link->column));
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
        this->log_markdown_blocks.append(flushed_markdown);
        this->session_controller->append_current_conversation_log_entry(flushed_markdown);
        this->actions_log_model->commit_streaming_entry(
            flushed_markdown, this->render_actions_log_markdown(flushed_markdown));
    }
    else
    {
        this->actions_log_model->clear_streaming_entry();
    }
    this->streaming_markdown.clear();
    this->streaming_response_raw.clear();
    this->is_streaming = false;
    this->sync_raw_markdown_view();
    this->scroll_log_views_to_bottom();
}

void agent_dock_widget_t::discard_streaming_markdown()
{
    if (this->render_throttle->isActive())
    {
        this->render_throttle->stop();
    }

    this->streaming_markdown.clear();
    this->streaming_response_raw.clear();
    this->is_streaming = false;
    this->last_committed_streaming_markdown.clear();
    this->actions_log_model->clear_streaming_entry();
    this->sync_raw_markdown_view();
}

void agent_dock_widget_t::render_log()
{
    if (this->is_streaming)
    {
        this->actions_log_model->set_streaming_entry(
            this->streaming_markdown, this->render_actions_log_markdown(this->streaming_markdown));
        this->sync_raw_markdown_view();
        this->scroll_log_views_to_bottom();
        return;
    }

    this->render_log_full();
}

void agent_dock_widget_t::render_log_full()
{
    const QStringList visible_blocks =
        this->current_log_markdown_blocks().mid(0, this->log_markdown_blocks.size());
    QStringList rendered_blocks;
    rendered_blocks.reserve(visible_blocks.size());
    for (const QString &block : visible_blocks)
    {
        rendered_blocks.append(this->render_actions_log_markdown(block));
    }
    this->actions_log_model->set_committed_entries(visible_blocks, rendered_blocks);
    if (this->streaming_markdown.isEmpty() == false)
    {
        this->actions_log_model->set_streaming_entry(
            this->streaming_markdown, this->render_actions_log_markdown(this->streaming_markdown));
    }
    this->sync_raw_markdown_view();
    this->scroll_log_views_to_bottom();
}

void agent_dock_widget_t::render_log_streaming(const QString &new_tokens)
{
    Q_UNUSED(new_tokens);
    this->render_log();
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
    this->tabs->setCurrentWidget(this->plan_page);
}

void agent_dock_widget_t::on_diff_available(const QString &diff)
{
    this->sync_diff_ui(diff, diff.isEmpty() == false, true);
}

void agent_dock_widget_t::update_approval_selection_ui()
{
    QListWidgetItem *item = this->approval_list->currentItem();
    if (item == nullptr)
    {
        this->approval_preview_view->clear();
        this->approve_action_btn->setEnabled(false);
        this->deny_action_btn->setEnabled(false);
        return;
    }

    const QString preview = item->data(approval_preview_role).toString();
    const bool pending = item->data(approval_pending_role).toBool();
    const bool inline_review = item->data(approval_inline_role).toBool();

    QString visible_preview = preview;
    if (inline_review == true && pending == true)
    {
        visible_preview += QStringLiteral(
            "\n\n---\nResolve this approval via inline accept/reject actions in the editor.");
    }

    this->approval_preview_view->setPlainText(visible_preview);
    this->approve_action_btn->setEnabled(pending == true && inline_review == false);
    this->deny_action_btn->setEnabled(pending == true && inline_review == false);
}

void agent_dock_widget_t::resolve_selected_approval(bool approved)
{
    QListWidgetItem *item = this->approval_list->currentItem();
    if (item == nullptr)
    {
        return;
    }

    const int approval_id = item->data(Qt::UserRole).toInt();
    const bool pending = item->data(approval_pending_role).toBool();
    const bool inline_review = item->data(approval_inline_role).toBool();
    if (pending == false || inline_review == true)
    {
        this->update_approval_selection_ui();
        return;
    }

    item->setData(approval_pending_role, false);
    item->setText(item->text() +
                  (approved == true ? QStringLiteral(" ✅") : QStringLiteral(" ❌")));
    this->approval_items.remove(approval_id);

    if (approved == true)
    {
        this->controller->approve_action(approval_id);
    }
    else
    {
        this->controller->deny_action(approval_id);
    }

    this->update_approval_selection_ui();
    this->session_controller->save_chat();
}

void agent_dock_widget_t::on_approval_requested(int id, const QString &action,
                                                const QString &reason, const QString &preview)
{
    const QString project_dir =
        review_project_dir(this->session_controller.get(), this->controller);
    const inline_approval_diff_t inline_diff =
        extract_inline_approval_diff(action, preview, project_dir);
    const bool has_inline_review = inline_diff.review_diff.trimmed().isEmpty() == false;

    auto *item = new QListWidgetItem(QStringLiteral("[#%1] %2 — %3").arg(id).arg(action, reason));
    item->setData(Qt::UserRole, id);
    item->setData(approval_preview_role,
                  has_inline_review == true ? inline_diff.full_diff : preview);
    item->setData(approval_pending_role, true);
    item->setData(approval_inline_role, has_inline_review);
    item->setToolTip(preview);
    this->approval_list->addItem(item);
    this->approval_items[id] = item;
    this->approval_list->setCurrentItem(item);

    if (has_inline_review == true)
    {
        this->active_inline_approval_id = id;
        this->on_log_message(
            QStringLiteral("📝 Review '%1' via inline diff annotations in the editor.")
                .arg(action));
        this->sync_diff_ui(inline_diff.review_diff, true, true);
        if (auto *page = this->diff_view->parentWidget())
        {
            this->tabs->setCurrentWidget(page);
        }
    }
    else
    {
        if (auto *page = this->approval_list->parentWidget())
        {
            this->tabs->setCurrentWidget(page);
        }
    }

    this->update_approval_selection_ui();
    this->session_controller->save_chat();
}

void agent_dock_widget_t::on_decision_requested(const agent_decision_request_t &request)
{
    if (this->decision_request_widget == nullptr)
    {
        return;
    }

    this->decision_request_widget->set_decision_request(request);
    this->status_label->setText(QStringLiteral("Waiting for user decision: %1")
                                    .arg(request.title.trimmed().isEmpty() == false
                                             ? request.title.trimmed()
                                             : QStringLiteral("Decision request")));
    this->session_controller->save_chat();
}

void agent_dock_widget_t::on_iteration_changed(int iteration)
{
    this->on_log_message(QStringLiteral("🔄 Iteration %1 / Tool calls: %2")
                             .arg(iteration)
                             .arg(this->controller->tool_call_count()));
}

void agent_dock_widget_t::on_stopped(const QString &summary)
{
    const agent_run_state_machine_t::state_t run_state = this->controller->current_run_state();
    this->update_run_state(false);
    this->active_inline_approval_id = -1;
    if (this->decision_request_widget != nullptr)
    {
        this->decision_request_widget->clear_request();
    }
    if (this->debugger_status_widget != nullptr)
    {
        this->debugger_status_widget->refresh();
    }
    if (this->render_throttle->isActive())
    {
        this->render_throttle->stop();
    }
    if (!this->streaming_markdown.isEmpty() || !this->streaming_response_raw.isEmpty())
    {
        this->flush_streaming_markdown();
    }
    if (this->status_label->text().isEmpty() == true ||
        this->status_label->text() == tr("Ready") ||
        this->status_label->text() == tr("Running..."))
    {
        this->status_label->setText(QStringLiteral("Done: %1").arg(summary.left(80)));
    }
    this->update_approval_selection_ui();
    this->session_controller->save_chat();

    if (run_state != agent_run_state_machine_t::state_t::COMPLETED)
    {
        this->deferred_request = queued_request_t{};
        this->has_deferred_request = false;
        return;
    }

    if (this->has_deferred_request == true)
    {
        const queued_request_t request = this->deferred_request;
        this->deferred_request = queued_request_t{};
        this->has_deferred_request = false;
        this->skip_auto_compact_once = true;
        QMetaObject::invokeMethod(
            this, [this, request]() { this->start_request(request); }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(
        this, [this]() { this->maybe_start_next_queued_request(); }, Qt::QueuedConnection);
}

void agent_dock_widget_t::submit_decision_option(const QString &request_id,
                                                 const QString &option_id)
{
    if (this->controller->submit_user_decision(request_id, option_id, {}) == true)
    {
        this->tabs->setCurrentWidget(this->actions_log_page);
    }
}

void agent_dock_widget_t::submit_decision_freeform(const QString &request_id,
                                                   const QString &freeform_text)
{
    if (this->controller->submit_user_decision(request_id, {}, freeform_text) == true)
    {
        this->tabs->setCurrentWidget(this->actions_log_page);
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
            this->actions_log_model->clear();
            this->raw_markdown_view->clear();
            this->log_markdown.clear();
            this->log_markdown_blocks.clear();
            this->streaming_markdown.clear();
            this->streaming_response_raw.clear();
            this->last_committed_streaming_markdown.clear();
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
