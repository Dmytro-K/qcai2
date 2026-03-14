/*! Implements the compact linked-files view. */

#include "LinkedFilesListWidget.h"

#include <QResizeEvent>

namespace qcai2
{

LinkedFilesListWidget::LinkedFilesListWidget(QWidget *parent) : QListWidget(parent)
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
    setStyleSheet(QStringLiteral(
        "QListWidget { background: palette(window); border: 0; padding: 2px; }"
        "QListWidget::item { padding: 2px 6px; }"));
}

void LinkedFilesListWidget::syncToContents()
{
    if (count() == 0 || !isVisible())
    {
        setFixedHeight(0);
        return;
    }

    const int availableWidth = qMax(1, viewport()->width());
    int totalHeight = 0;
    int currentRowWidth = 0;
    int currentRowHeight = 0;

    for (int index = 0; index < count(); ++index)
    {
        const QListWidgetItem *item = this->item(index);
        const QSize itemSize = item != nullptr ? item->sizeHint() : QSize();
        const int itemWidth = qMax(1, itemSize.width());
        const int itemHeight = qMax(fontMetrics().height() + 8, itemSize.height());

        if (currentRowWidth > 0 && currentRowWidth + spacing() + itemWidth > availableWidth)
        {
            totalHeight += currentRowHeight + spacing();
            currentRowWidth = itemWidth;
            currentRowHeight = itemHeight;
            continue;
        }

        currentRowWidth += (currentRowWidth > 0 ? spacing() : 0) + itemWidth;
        currentRowHeight = qMax(currentRowHeight, itemHeight);
    }

    totalHeight += qMax(currentRowHeight, fontMetrics().height() + 8);
    totalHeight += 4;
    setFixedHeight(totalHeight);
}

void LinkedFilesListWidget::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    syncToContents();
}

}  // namespace qcai2
