/*! Declares the selectable read-only text view used inside inline diff previews. */
#pragma once

#include <QPlainTextEdit>
#include <QStringList>

class QMouseEvent;

namespace qcai2
{

/**
 * Read-only code-style text view for inline diff previews.
 */
class diff_preview_text_edit_t final : public QPlainTextEdit
{
public:
    /**
     * Creates a fixed-height preview editor for diff lines.
     * @param preview_lines Diff preview lines to display.
     * @param parent Owning widget.
     */
    explicit diff_preview_text_edit_t(const QStringList &preview_lines, QWidget *parent = nullptr);

protected:
    /**
     * Keeps keyboard focus on the preview when the user starts selecting text
     * with the mouse inside the embedded diff widget.
     * @param event Mouse press event.
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * Restores keyboard focus after mouse selection completes so copy
     * shortcuts route to the preview instead of the surrounding editor.
     * @param event Mouse release event.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
};

}  // namespace qcai2
