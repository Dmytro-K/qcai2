/*! Implements the shared special-token-aware goal editor. */

#include "goal_text_edit.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextCursor>
#include <QUrl>

namespace qcai2
{

goal_text_edit_t::goal_text_edit_t(QWidget *parent) : QTextEdit(parent)
{
    setAcceptDrops(true);
    this->completion_model = new QStandardItemModel(this);
    this->completer = new QCompleter(this->completion_model, this);
    this->completer->setCaseSensitivity(Qt::CaseInsensitive);
    this->completer->setCompletionMode(QCompleter::PopupCompletion);
    this->completer->setFilterMode(Qt::MatchStartsWith);
    this->completer->setWidget(this);

    connect(this->completer, qOverload<const QModelIndex &>(&QCompleter::activated), this,
            [this](const QModelIndex &index) { this->apply_completion_index(index); });
    connect(this, &QTextEdit::textChanged, this, [this]() { this->refresh_special_state(); });
    connect(this, &QTextEdit::cursorPositionChanged, this,
            [this]() { this->refresh_special_state(); });
}

void goal_text_edit_t::add_special_handler(std::unique_ptr<goal_special_handler_t> handler)
{
    if (((!handler) == true))
    {
        return;
    }

    this->special_handlers.push_back(std::move(handler));
    this->refresh_special_state();
}

bool goal_text_edit_t::has_completion_popup() const
{
    return this->completer != nullptr && this->completer->popup() != nullptr &&
           this->completer->popup()->isVisible();
}

void goal_text_edit_t::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return;
    }

    QTextEdit::dragEnterEvent(event);
}

void goal_text_edit_t::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return;
    }

    QTextEdit::dragMoveEvent(event);
}

void goal_text_edit_t::dropEvent(QDropEvent *event)
{
    if (((event->mimeData()->hasUrls()) == true))
    {
        QStringList paths;
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls)
        {
            if (url.isLocalFile())
            {
                paths.append(url.toLocalFile());
            }
        }

        if (((!paths.isEmpty()) == true))
        {
            emit this->files_dropped(paths);
            event->acceptProposedAction();
            return;
        }
    }

    QTextEdit::dropEvent(event);
}

void goal_text_edit_t::keyPressEvent(QKeyEvent *event)
{
    if (this->has_completion_popup() == true)
    {
        switch (event->key())
        {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                event->ignore();
                return;
            default:
                break;
        }
    }

    QTextEdit::keyPressEvent(event);
}

void goal_text_edit_t::refresh_special_state()
{
    this->refresh_highlighting();
    this->refresh_completion_popup();
}

void goal_text_edit_t::refresh_completion_popup()
{
    this->active_completion = {};

    const goal_completion_request_t request{toPlainText(), textCursor().position()};
    for (const auto &handler : this->special_handlers)
    {
        const goal_completion_session_t session = handler->completion_session(request);
        if (session.active)
        {
            this->active_completion = session;
            break;
        }
    }

    if (((!this->active_completion.active || this->active_completion.items.isEmpty()) == true))
    {
        this->completion_model->clear();
        if (((this->completer != nullptr && this->completer->popup() != nullptr) == true))
        {
            this->completer->popup()->hide();
        }
        return;
    }

    this->completion_model->clear();
    for (const goal_completion_item_t &item : this->active_completion.items)
    {
        auto *model_item = new QStandardItem(item.label.isEmpty() ? item.insert_text : item.label);
        model_item->setData(item.insert_text, Qt::UserRole);
        if (!item.detail.isEmpty())
        {
            model_item->setToolTip(item.detail);
        }
        this->completion_model->appendRow(model_item);
    }

    if (((this->completer == nullptr || this->completer->popup() == nullptr) == true))
    {
        return;
    }

    this->completer->setCompletionPrefix(QString());

    QRect popup_rect = cursorRect();
    popup_rect.setWidth(this->completer->popup()->sizeHintForColumn(0) +
                        this->completer->popup()->verticalScrollBar()->sizeHint().width() + 16);
    this->completer->complete(popup_rect);
}

void goal_text_edit_t::refresh_highlighting()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QString text = toPlainText();
    const QPalette current_palette = palette();

    for (const auto &handler : this->special_handlers)
    {
        const QList<goal_highlight_span_t> spans = handler->highlight_spans(text, current_palette);
        for (const goal_highlight_span_t &span : spans)
        {
            if (span.length <= 0)
            {
                continue;
            }

            QTextCursor cursor(document());
            cursor.setPosition(span.start);
            cursor.setPosition(span.start + span.length, QTextCursor::KeepAnchor);

            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            selection.format = span.format;
            selections.append(selection);
        }
    }

    setExtraSelections(selections);
}

void goal_text_edit_t::apply_completion_index(const QModelIndex &index)
{
    if (((!this->active_completion.active || !index.isValid()) == true))
    {
        return;
    }

    const QString insert_text = index.data(Qt::UserRole).toString();
    if (insert_text.isEmpty() == true)
    {
        return;
    }

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(this->active_completion.replace_start);
    cursor.setPosition(this->active_completion.replace_end, QTextCursor::KeepAnchor);
    cursor.insertText(insert_text);
    cursor.endEditBlock();
    setTextCursor(cursor);

    if (((this->completer != nullptr && this->completer->popup() != nullptr) == true))
    {
        this->completer->popup()->hide();
    }
}

}  // namespace qcai2
