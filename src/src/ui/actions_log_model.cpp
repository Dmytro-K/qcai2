/*! @file
    @brief Implements the list model used by the Actions Log tab.
*/

#include "actions_log_model.h"

#include <algorithm>

namespace qcai2
{

actions_log_model_t::actions_log_model_t(QObject *parent) : QAbstractListModel(parent)
{
}

int actions_log_model_t::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(this->entries.size());
}

QVariant actions_log_model_t::data(const QModelIndex &index, int role) const
{
    if (index.isValid() == false || index.row() < 0 || index.row() >= this->entries.size())
    {
        return {};
    }

    const entry_t &entry = this->entries.at(index.row());
    switch (role)
    {
        case Qt::DisplayRole:
        case RENDERED_MARKDOWN_ROLE:
            return entry.rendered_markdown;
        case RAW_MARKDOWN_ROLE:
            return entry.raw_markdown;
        case ENTRY_ID_ROLE:
            return entry.entry_id;
        case STREAMING_ROLE:
            return entry.streaming;
        default:
            return {};
    }
}

void actions_log_model_t::clear()
{
    if (this->entries.isEmpty() == true)
    {
        return;
    }

    this->beginResetModel();
    this->entries.clear();
    this->endResetModel();
}

void actions_log_model_t::set_committed_entries(const QStringList &raw_markdown_blocks,
                                                const QStringList &rendered_markdown_blocks)
{
    Q_ASSERT(raw_markdown_blocks.size() == rendered_markdown_blocks.size());

    this->beginResetModel();
    this->entries.clear();
    this->entries.reserve(raw_markdown_blocks.size());
    for (qsizetype i = 0; i < raw_markdown_blocks.size(); ++i)
    {
        this->entries.append(
            this->make_entry(raw_markdown_blocks.at(i), rendered_markdown_blocks.at(i), false));
    }
    this->endResetModel();
}

void actions_log_model_t::append_committed_entry(const QString &raw_markdown,
                                                 const QString &rendered_markdown)
{
    const int insert_row = static_cast<int>(this->entries.size());
    this->beginInsertRows(QModelIndex(), insert_row, insert_row);
    this->entries.append(this->make_entry(raw_markdown, rendered_markdown, false));
    this->endInsertRows();
}

void actions_log_model_t::set_streaming_entry(const QString &raw_markdown,
                                              const QString &rendered_markdown)
{
    if (raw_markdown.isEmpty() == true)
    {
        this->clear_streaming_entry();
        return;
    }

    if (this->entries.isEmpty() == false && this->entries.constLast().streaming == true)
    {
        entry_t &entry = this->entries.last();
        entry.raw_markdown = raw_markdown;
        entry.rendered_markdown = rendered_markdown;
        const QModelIndex row_index = this->index(static_cast<int>(this->entries.size() - 1), 0);
        emit this->dataChanged(row_index, row_index,
                               {Qt::DisplayRole, RAW_MARKDOWN_ROLE, RENDERED_MARKDOWN_ROLE});
        return;
    }

    const int insert_row = static_cast<int>(this->entries.size());
    this->beginInsertRows(QModelIndex(), insert_row, insert_row);
    this->entries.append(this->make_entry(raw_markdown, rendered_markdown, true));
    this->endInsertRows();
}

void actions_log_model_t::clear_streaming_entry()
{
    if (this->entries.isEmpty() == true || this->entries.constLast().streaming == false)
    {
        return;
    }

    const int remove_row = static_cast<int>(this->entries.size() - 1);
    this->beginRemoveRows(QModelIndex(), remove_row, remove_row);
    this->entries.removeLast();
    this->endRemoveRows();
}

void actions_log_model_t::commit_streaming_entry(const QString &raw_markdown,
                                                 const QString &rendered_markdown)
{
    if (this->entries.isEmpty() == false && this->entries.constLast().streaming == true)
    {
        entry_t &entry = this->entries.last();
        entry.raw_markdown = raw_markdown;
        entry.rendered_markdown = rendered_markdown;
        entry.streaming = false;
        const QModelIndex row_index = this->index(static_cast<int>(this->entries.size() - 1), 0);
        emit this->dataChanged(
            row_index, row_index,
            {Qt::DisplayRole, RAW_MARKDOWN_ROLE, RENDERED_MARKDOWN_ROLE, STREAMING_ROLE});
        return;
    }

    this->append_committed_entry(raw_markdown, rendered_markdown);
}

QString actions_log_model_t::selected_raw_markdown(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty() == true)
    {
        return {};
    }

    QModelIndexList sorted_indexes = indexes;
    std::sort(
        sorted_indexes.begin(), sorted_indexes.end(),
        [](const QModelIndex &lhs, const QModelIndex &rhs) { return lhs.row() < rhs.row(); });

    QStringList blocks;
    blocks.reserve(sorted_indexes.size());
    for (const QModelIndex &index : std::as_const(sorted_indexes))
    {
        if (index.isValid() == false || index.row() < 0 || index.row() >= this->entries.size())
        {
            continue;
        }
        blocks.append(this->entries.at(index.row()).raw_markdown);
    }
    return blocks.join(QStringLiteral("\n\n"));
}

actions_log_model_t::entry_t actions_log_model_t::make_entry(const QString &raw_markdown,
                                                             const QString &rendered_markdown,
                                                             bool streaming) const
{
    return {QStringLiteral("entry-%1").arg(this->next_entry_id++), raw_markdown, rendered_markdown,
            streaming};
}

}  // namespace qcai2
