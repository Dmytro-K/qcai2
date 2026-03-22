/*! Implements a QListWidget that hides itself when it has no rows. */

#include "auto_hiding_list_widget.h"

namespace qcai2
{

auto_hiding_list_widget_t::auto_hiding_list_widget_t(QWidget *parent) : QListWidget(parent)
{
    this->setVisible(false);
}

void auto_hiding_list_widget_t::refresh_visibility()
{
    this->setVisible(this->count() > 0);
}

}  // namespace qcai2
