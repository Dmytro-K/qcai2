/*! @file
    @brief Implements painting for inline ghost-text overlays.
*/

#include "GhostTextOverlay.h"

#include <texteditor/texteditor.h>

#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

namespace qcai2
{

GhostTextOverlay::GhostTextOverlay(TextEditor::TextEditorWidget *editor)
    : QWidget(editor->viewport()), m_editor(editor)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void GhostTextOverlay::showGhostText(const QString &text, int cursorPosition)
{
    m_ghostText = text;
    m_cursorPosition = cursorPosition;
    reposition();
    show();
    raise();
    update();
}

void GhostTextOverlay::hideGhostText()
{
    if (m_ghostText.isEmpty())
        return;
    m_ghostText.clear();
    m_cursorPosition = -1;
    hide();
}

void GhostTextOverlay::reposition()
{
    if (m_editor && m_editor->viewport())
        setGeometry(m_editor->viewport()->rect());
}

void GhostTextOverlay::paintEvent(QPaintEvent *)
{
    if (m_ghostText.isEmpty() || !m_editor)
        return;

    QTextCursor tc = m_editor->textCursor();
    if (tc.position() != m_cursorPosition)
    {
        hideGhostText();
        return;
    }

    const QRect cursorRect = m_editor->cursorRect(tc);
    if (!rect().intersects(cursorRect))
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(m_editor->font());

    QColor ghostColor = m_editor->palette().color(QPalette::Text);
    ghostColor.setAlpha(90);
    painter.setPen(ghostColor);

    QColor bgColor = m_editor->palette().color(QPalette::Base);
    bgColor.setAlpha(220);

    const QFontMetrics fm(m_editor->font());
    const QStringList lines = m_ghostText.split(QLatin1Char('\n'));
    const int lineHeight = cursorRect.height();

    int x = cursorRect.right() + 1;
    int y = cursorRect.top();

    if (!lines.isEmpty())
        painter.drawText(x, y + fm.ascent(), lines.first());

    if (lines.size() > 1)
    {
        QTextCursor sob = tc;
        sob.movePosition(QTextCursor::StartOfBlock);
        const int lineStartX = m_editor->cursorRect(sob).left();

        for (int i = 1; i < lines.size(); ++i)
        {
            y += lineHeight;
            if (y > height())
                break;
            const int tw = fm.horizontalAdvance(lines.at(i));
            if (tw > 0)
                painter.fillRect(lineStartX, y, tw + 2, lineHeight, bgColor);
            painter.drawText(lineStartX, y + fm.ascent(), lines.at(i));
        }
    }
}

}  // namespace qcai2
