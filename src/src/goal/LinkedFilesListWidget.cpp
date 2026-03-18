/*! Implements the compact linked-files view. */

#include "LinkedFilesListWidget.h"

#include <QResizeEvent>

namespace qcai2
{

linked_files_list_widget_t::linked_files_list_widget_t(QWidget *parent) : QListWidget(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSpacing(4);
    setIconSize(QSize(16, 16));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(
        QStringLiteral("QListWidget { background: palette(window); border: 0; padding: 2px; }"
                       "QListWidget::item { padding: 2px 6px; }"));
}

void linked_files_list_widget_t::sync_to_contents()
{
    if (count() == 0 || !isVisible())
    {
        setFixedHeight(0);
        return;
    }

    const int available_width = qMax(1, viewport()->width());
    int total_height = 0;
    int current_row_width = 0;
    int current_row_height = 0;

    for (int index = 0; index < count(); ++index)
    {
        const QListWidgetItem *item = this->item(index);
        const QSize item_size = item != nullptr ? item->sizeHint() : QSize();
        const int item_width = qMax(1, item_size.width());
        const int item_height = qMax(fontMetrics().height() + 8, item_size.height());

        if (current_row_width > 0 && current_row_width + spacing() + item_width > available_width)
        {
            total_height += current_row_height + spacing();
            current_row_width = item_width;
            current_row_height = item_height;
            continue;
        }

        current_row_width += (current_row_width > 0 ? spacing() : 0) + item_width;
        current_row_height = qMax(current_row_height, item_height);
    }

    total_height += qMax(current_row_height, fontMetrics().height() + 8);
    total_height += 4;
    setFixedHeight(total_height);
}

void linked_files_list_widget_t::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    this->sync_to_contents();
}

}  // namespace qcai2
