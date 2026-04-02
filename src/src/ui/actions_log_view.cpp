/*! @file
    @brief Implements the list view used by the Actions Log tab.
*/

#include "actions_log_view.h"

#include <QAbstractItemModel>

namespace qcai2
{

actions_log_view_t::actions_log_view_t(QWidget *parent) : QListView(parent)
{
}

void actions_log_view_t::setModel(QAbstractItemModel *model)
{
    this->clear_persistent_editors();
    for (const QMetaObject::Connection &connection : std::as_const(this->model_connections))
    {
        QObject::disconnect(connection);
    }
    this->model_connections.clear();

    QListView::setModel(model);
    if (model == nullptr)
    {
        return;
    }

    this->model_connections.append(connect(model, &QAbstractItemModel::modelReset, this,
                                           &actions_log_view_t::rebuild_persistent_editors));
    this->model_connections.append(connect(model, &QAbstractItemModel::rowsInserted, this,
                                           [this](const QModelIndex &parent, int first, int last) {
                                               if (parent.isValid() == true)
                                               {
                                                   return;
                                               }
                                               this->open_persistent_editors_for_rows(first, last);
                                           }));
    this->model_connections.append(connect(model, &QAbstractItemModel::rowsRemoved, this,
                                           [this](const QModelIndex &parent, int first, int last) {
                                               Q_UNUSED(parent);
                                               Q_UNUSED(first);
                                               Q_UNUSED(last);
                                               this->prune_invalid_persistent_editors();
                                           }));
    this->model_connections.append(connect(model, &QAbstractItemModel::layoutChanged, this,
                                           &actions_log_view_t::rebuild_persistent_editors));

    this->rebuild_persistent_editors();
}

void actions_log_view_t::rebuild_persistent_editors()
{
    this->clear_persistent_editors();
    if (this->model() == nullptr)
    {
        return;
    }

    this->open_persistent_editors_for_rows(0, this->model()->rowCount() - 1);
}

void actions_log_view_t::open_persistent_editors_for_rows(int first_row, int last_row)
{
    if (this->model() == nullptr || first_row > last_row)
    {
        return;
    }

    const int row_count = this->model()->rowCount();
    for (int row = qMax(0, first_row); row <= qMin(last_row, row_count - 1); ++row)
    {
        const QModelIndex index = this->model()->index(row, 0);
        if (index.isValid() == false)
        {
            continue;
        }

        const QPersistentModelIndex persistent_index(index);
        if (this->persistent_editor_indexes.contains(persistent_index) == true)
        {
            continue;
        }

        this->openPersistentEditor(index);
        this->persistent_editor_indexes.append(persistent_index);
    }
}

void actions_log_view_t::clear_persistent_editors()
{
    for (const QPersistentModelIndex &index : std::as_const(this->persistent_editor_indexes))
    {
        if (index.isValid() == true)
        {
            this->closePersistentEditor(index);
        }
    }
    this->persistent_editor_indexes.clear();
}

void actions_log_view_t::prune_invalid_persistent_editors()
{
    for (auto it = this->persistent_editor_indexes.begin();
         it != this->persistent_editor_indexes.end();)
    {
        if (it->isValid() == false || it->model() != this->model())
        {
            it = this->persistent_editor_indexes.erase(it);
            continue;
        }
        ++it;
    }
}

}  // namespace qcai2
