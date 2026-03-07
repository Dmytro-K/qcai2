#pragma once

#include <QString>
#include <QWidget>

namespace TextEditor
{
class TextEditorWidget;
}

namespace qcai2
{

/**
 * Paints translucent ghost text over an editor viewport.
 */
class GhostTextOverlay : public QWidget
{
    Q_OBJECT
public:
    /**
     * Creates an overlay for an editor viewport.
     * @param editor Editor widget that owns the viewport.
     */
    explicit GhostTextOverlay(TextEditor::TextEditorWidget *editor);

    /**
     * Displays ghost text at the current cursor position.
     * @param text Suggested text to paint.
     * @param cursorPosition Cursor position the suggestion is anchored to.
     */
    void showGhostText(const QString &text, int cursorPosition);

    /**
     * Hides the current ghost text, if any.
     */
    void hideGhostText();

    /**
     * Returns the currently displayed ghost text.
     * @return Inline suggestion text.
     */
    QString ghostText() const
    {
        return m_ghostText;
    }

    /**
     * Returns the cursor position used to anchor the suggestion.
     * @return Document position tracked by the overlay.
     */
    int cursorPosition() const
    {
        return m_cursorPosition;
    }

    /**
     * Returns whether ghost text is currently visible.
     * @return True when the overlay holds suggestion text.
     */
    bool hasGhostText() const
    {
        return !m_ghostText.isEmpty();
    }

protected:
    /**
     * Paints the overlay contents.
     * @param event Paint event from Qt.
     */
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * Resizes the overlay to match the editor viewport.
     */
    void reposition();

    /** Non-owning editor whose viewport hosts the overlay. */
    TextEditor::TextEditorWidget *m_editor;
    /** Current ghost-text content. */
    QString m_ghostText;
    /** Cursor position the current suggestion is anchored to. */
    int m_cursorPosition = -1;
};

}  // namespace qcai2
