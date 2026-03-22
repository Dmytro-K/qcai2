/*! Declares a QListWidget that hides itself when it has no rows. */
#pragma once

#include <QListWidget>

namespace qcai2
{

/**
 * Small reusable list widget that auto-hides when there is nothing to show.
 */
class auto_hiding_list_widget_t : public QListWidget
{
    Q_OBJECT

public:
    explicit auto_hiding_list_widget_t(QWidget *parent = nullptr);

    /** Recomputes visibility from the current item count. */
    void refresh_visibility();
};

}  // namespace qcai2
