/*! @file
    @brief Declares the list view used by the Actions Log tab.
*/
#pragma once

#include <QListView>
#include <QPersistentModelIndex>

namespace qcai2
{

/**
 * Keeps Actions Log rows interactive by opening persistent editors for visible list entries as
 * soon as they appear in the model.
 */
class actions_log_view_t final : public QListView
{
    Q_OBJECT
public:
    /**
     * Creates the Actions Log view.
     * @param parent Optional parent widget.
     */
    explicit actions_log_view_t(QWidget *parent = nullptr);

    /**
     * Attaches one model and wires automatic persistent-editor lifecycle for its rows.
     * @param model Model that backs the Actions Log list.
     */
    void setModel(QAbstractItemModel *model) override;

private:
    /**
     * Recreates the set of persistent row editors for the current model contents.
     */
    void rebuild_persistent_editors();

    /**
     * Opens persistent editors for the inclusive row range.
     * @param first_row First row to open.
     * @param last_row Last row to open.
     */
    void open_persistent_editors_for_rows(int first_row, int last_row);

    /**
     * Closes any tracked persistent editors and resets local tracking state.
     */
    void clear_persistent_editors();

    /**
     * Removes stale tracked indexes after rows disappear from the model.
     */
    void prune_invalid_persistent_editors();

    /** Indexes whose row editors are currently kept persistent. */
    QList<QPersistentModelIndex> persistent_editor_indexes;

    /** Model-signal connections that keep persistent editors in sync with row changes. */
    QList<QMetaObject::Connection> model_connections;
};

}  // namespace qcai2
