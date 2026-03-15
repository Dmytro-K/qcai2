/*! Implements the shared special-token-aware goal editor. */

#include "GoalTextEdit.h"

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

GoalTextEdit::GoalTextEdit(QWidget *parent) : QTextEdit(parent)
{
    setAcceptDrops(true);
    m_completionModel = new QStandardItemModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setWidget(this);

    connect(m_completer, qOverload<const QModelIndex &>(&QCompleter::activated), this,
            [this](const QModelIndex &index) { applyCompletionIndex(index); });
    connect(this, &QTextEdit::textChanged, this, [this]() { refreshSpecialState(); });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() { refreshSpecialState(); });
}

void GoalTextEdit::addSpecialHandler(std::unique_ptr<GoalSpecialHandler> handler)
{
    if (((!handler) == true))
    {
        return;
    }

    m_specialHandlers.push_back(std::move(handler));
    refreshSpecialState();
}

bool GoalTextEdit::hasCompletionPopup() const
{
    return m_completer != nullptr && m_completer->popup() != nullptr &&
           m_completer->popup()->isVisible();
}

void GoalTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return;
    }

    QTextEdit::dragEnterEvent(event);
}

void GoalTextEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return;
    }

    QTextEdit::dragMoveEvent(event);
}

void GoalTextEdit::dropEvent(QDropEvent *event)
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
            emit filesDropped(paths);
            event->acceptProposedAction();
            return;
        }
    }

    QTextEdit::dropEvent(event);
}

void GoalTextEdit::keyPressEvent(QKeyEvent *event)
{
    if (hasCompletionPopup() == true)
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

void GoalTextEdit::refreshSpecialState()
{
    refreshHighlighting();
    refreshCompletionPopup();
}

void GoalTextEdit::refreshCompletionPopup()
{
    m_activeCompletion = {};

    const GoalCompletionRequest request{toPlainText(), textCursor().position()};
    for (const auto &handler : m_specialHandlers)
    {
        const GoalCompletionSession session = handler->completionSession(request);
        if (session.active)
        {
            m_activeCompletion = session;
            break;
        }
    }

    if (((!m_activeCompletion.active || m_activeCompletion.items.isEmpty()) == true))
    {
        m_completionModel->clear();
        if (((m_completer != nullptr && m_completer->popup() != nullptr) == true))
        {
            m_completer->popup()->hide();
        }
        return;
    }

    m_completionModel->clear();
    for (const GoalCompletionItem &item : m_activeCompletion.items)
    {
        auto *modelItem = new QStandardItem(item.label.isEmpty() ? item.insertText : item.label);
        modelItem->setData(item.insertText, Qt::UserRole);
        if (!item.detail.isEmpty())
        {
            modelItem->setToolTip(item.detail);
        }
        m_completionModel->appendRow(modelItem);
    }

    if (((m_completer == nullptr || m_completer->popup() == nullptr) == true))
    {
        return;
    }

    m_completer->setCompletionPrefix(QString());

    QRect popupRect = cursorRect();
    popupRect.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                       m_completer->popup()->verticalScrollBar()->sizeHint().width() + 16);
    m_completer->complete(popupRect);
}

void GoalTextEdit::refreshHighlighting()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QString text = toPlainText();
    const QPalette currentPalette = palette();

    for (const auto &handler : m_specialHandlers)
    {
        const QList<GoalHighlightSpan> spans = handler->highlightSpans(text, currentPalette);
        for (const GoalHighlightSpan &span : spans)
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

void GoalTextEdit::applyCompletionIndex(const QModelIndex &index)
{
    if (((!m_activeCompletion.active || !index.isValid()) == true))
    {
        return;
    }

    const QString insertText = index.data(Qt::UserRole).toString();
    if (insertText.isEmpty() == true)
    {
        return;
    }

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(m_activeCompletion.replaceStart);
    cursor.setPosition(m_activeCompletion.replaceEnd, QTextCursor::KeepAnchor);
    cursor.insertText(insertText);
    cursor.endEditBlock();
    setTextCursor(cursor);

    if (((m_completer != nullptr && m_completer->popup() != nullptr) == true))
    {
        m_completer->popup()->hide();
    }
}

}  // namespace qcai2
