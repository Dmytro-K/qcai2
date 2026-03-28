/*! @file
    @brief Declares the custom delegate used to paint one Actions Log row.
*/
#pragma once

#include <QStyledItemDelegate>

#include <functional>

namespace qcai2
{

/**
 * Paints one Actions Log entry as markdown-backed rich text inside a list row.
 */
class actions_log_delegate_t final : public QStyledItemDelegate
{
public:
    /**
     * Creates one Actions Log delegate.
     * @param parent Owning QObject.
     */
    explicit actions_log_delegate_t(QObject *parent = nullptr);

    /**
     * Returns the preferred size for one Actions Log row.
     * @param option View style and geometry hints.
     * @param index Row index to size.
     * @return Preferred item size.
     */
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /**
     * Paints one Actions Log row.
     * @param painter Target painter.
     * @param option View style and geometry hints.
     * @param index Row index to paint.
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /**
     * Creates the read-only rich-text editor used for the interactive row so text can be selected.
     * @param parent Parent widget provided by the view.
     * @param option View style and geometry hints.
     * @param index Row index that will host the editor.
     * @return One configured read-only text widget.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    /**
     * Updates the interactive row editor with the row markdown.
     * @param editor Editor widget created by createEditor().
     * @param index Row index whose data should be shown.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

    /**
     * Updates the geometry of the interactive row editor to match the painted row padding.
     * @param editor Editor widget created by createEditor().
     * @param option View style and geometry hints.
     * @param index Row index whose geometry is being updated.
     */
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;

    /**
     * Handles clicks on markdown anchors painted directly inside list rows.
     * @param event Mouse event forwarded by the view.
     * @param model Backing model for the row.
     * @param option View style and geometry hints.
     * @param index Row index that received the event.
     * @return True when the event activated an internal link.
     */
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

    /**
     * Sets the callback invoked when one rendered Actions Log link is activated.
     * @param handler Callback that receives the clicked href string.
     */
    void set_link_activated_handler(std::function<void(const QString &href)> handler);

private:
    /**
     * Returns the usable markdown text width for one row.
     * @param option View style and geometry hints.
     * @return Usable text width in device-independent pixels.
     */
    int text_width_for_option(const QStyleOptionViewItem &option) const;

    /** Callback invoked when one rendered Actions Log link is activated. */
    std::function<void(const QString &href)> link_activated_handler;
};

}  // namespace qcai2
