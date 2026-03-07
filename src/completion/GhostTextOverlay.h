#pragma once

#include <QString>
#include <QWidget>

namespace TextEditor
{
class TextEditorWidget;
}

namespace qcai2
{

// Transparent overlay that paints gray "ghost text" at the cursor position.
class GhostTextOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit GhostTextOverlay(TextEditor::TextEditorWidget *editor);

    void showGhostText(const QString &text, int cursorPosition);
    void hideGhostText();

    QString ghostText() const
    {
        return m_ghostText;
    }
    int cursorPosition() const
    {
        return m_cursorPosition;
    }
    bool hasGhostText() const
    {
        return !m_ghostText.isEmpty();
    }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void reposition();

    TextEditor::TextEditorWidget *m_editor;
    QString m_ghostText;
    int m_cursorPosition = -1;
};

}  // namespace qcai2
