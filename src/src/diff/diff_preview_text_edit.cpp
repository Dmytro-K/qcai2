/*! Implements the selectable read-only text view used inside inline diff previews. */
#include "diff_preview_text_edit.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPalette>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QTextFormat>

#include <cmath>

namespace qcai2
{

diff_preview_text_edit_t::diff_preview_text_edit_t(const QStringList &preview_lines,
                                                   QWidget *parent)
    : QPlainTextEdit(parent)
{
    this->setObjectName(QStringLiteral("qcai2DiffPreviewTextEdit"));
    this->setReadOnly(true);
    this->setUndoRedoEnabled(false);
    this->setFrameStyle(QFrame::NoFrame);
    this->setLineWrapMode(QPlainTextEdit::NoWrap);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    this->setFocusPolicy(Qt::StrongFocus);
    this->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    this->document()->setDocumentMargin(4);
    this->setViewportMargins(0, 0, 0, 0);
    this->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#qcai2DiffPreviewTextEdit { background: transparent; border: none; }"));

    auto *copy_shortcut = new QShortcut(QKeySequence::Copy, this);
    copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(copy_shortcut, &QShortcut::activated, this, [this]() {
        if (this->textCursor().hasSelection() == true)
        {
            QGuiApplication::clipboard()->setText(this->textCursor().selectedText().replace(
                QChar::ParagraphSeparator, QLatin1Char('\n')));
        }
    });

    this->setPlainText(preview_lines.join(QLatin1Char('\n')));

    QList<QTextEdit::ExtraSelection> extra_selections;
    QTextBlock block = this->document()->firstBlock();
    for (const QString &line : preview_lines)
    {
        if (block.isValid() == false)
        {
            break;
        }

        QTextCharFormat format;
        format.setProperty(QTextFormat::FullWidthSelection, true);
        if (line.startsWith(QLatin1Char('+')) == true &&
            line.startsWith(QStringLiteral("+++")) == false)
        {
            format.setBackground(QColor(70, 160, 95, 44));
        }
        else if (line.startsWith(QLatin1Char('-')) == true &&
                 line.startsWith(QStringLiteral("---")) == false)
        {
            format.setBackground(QColor(190, 80, 80, 44));
        }
        else
        {
            format.setBackground(QGuiApplication::palette().color(QPalette::AlternateBase));
        }

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        selection.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        selection.format = format;
        extra_selections.append(selection);
        block = block.next();
    }
    this->setExtraSelections(extra_selections);

    const int block_count = qMax(1, this->document()->blockCount());
    const int line_height = QFontMetrics(this->font()).lineSpacing();
    const int margins_height = int(std::ceil(this->document()->documentMargin() * 2.0));
    const int frame_height = this->frameWidth() * 2;
    this->setFixedHeight(block_count * line_height + margins_height + frame_height + 2);
}

void diff_preview_text_edit_t::mousePressEvent(QMouseEvent *event)
{
    this->setFocus(Qt::MouseFocusReason);
    QPlainTextEdit::mousePressEvent(event);
}

void diff_preview_text_edit_t::mouseReleaseEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseReleaseEvent(event);
    this->setFocus(Qt::MouseFocusReason);
}

}  // namespace qcai2
