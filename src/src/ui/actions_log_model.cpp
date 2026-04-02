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
    return parent.isValid()
               ? 0
               : static_cast<int>(this->committed_entries.size() + this->streaming_entries.size());
}

QVariant actions_log_model_t::data(const QModelIndex &index, int role) const
{
    if (index.isValid() == false || index.row() < 0 || index.row() >= this->rowCount())
    {
        return {};
    }

    const int committed_count = static_cast<int>(this->committed_entries.size());
    const entry_t &entry = index.row() < committed_count
                               ? this->committed_entries.at(index.row())
                               : this->streaming_entries.at(index.row() - committed_count);
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
    if (this->committed_entries.isEmpty() == true && this->streaming_entries.isEmpty() == true)
    {
        return;
    }

    this->beginResetModel();
    this->committed_entries.clear();
    this->streaming_entries.clear();
    this->endResetModel();
}

void actions_log_model_t::set_committed_entries(const QStringList &raw_markdown_blocks,
                                                const QStringList &rendered_markdown_blocks)
{
    Q_ASSERT(raw_markdown_blocks.size() == rendered_markdown_blocks.size());

    this->beginResetModel();
    this->committed_entries.clear();
    this->streaming_entries.clear();
    this->committed_entries.reserve(raw_markdown_blocks.size());
    for (qsizetype i = 0; i < raw_markdown_blocks.size(); ++i)
    {
        this->committed_entries.append(
            this->make_entry(raw_markdown_blocks.at(i), rendered_markdown_blocks.at(i), false));
    }
    this->endResetModel();
}

void actions_log_model_t::append_committed_entry(const QString &raw_markdown,
                                                 const QString &rendered_markdown)
{
    const int insert_row = static_cast<int>(this->committed_entries.size());
    this->beginInsertRows(QModelIndex(), insert_row, insert_row);
    this->committed_entries.append(this->make_entry(raw_markdown, rendered_markdown, false));
    this->endInsertRows();
}

void actions_log_model_t::set_streaming_entries(const QStringList &raw_markdown_blocks,
                                                const QStringList &rendered_markdown_blocks)
{
    Q_ASSERT(raw_markdown_blocks.size() == rendered_markdown_blocks.size());

    QStringList non_empty_raw_markdown_blocks;
    QStringList non_empty_rendered_markdown_blocks;
    non_empty_raw_markdown_blocks.reserve(raw_markdown_blocks.size());
    non_empty_rendered_markdown_blocks.reserve(rendered_markdown_blocks.size());
    for (qsizetype index = 0; index < raw_markdown_blocks.size(); ++index)
    {
        if (raw_markdown_blocks.at(index).trimmed().isEmpty() == true)
        {
            continue;
        }
        non_empty_raw_markdown_blocks.append(raw_markdown_blocks.at(index));
        non_empty_rendered_markdown_blocks.append(rendered_markdown_blocks.at(index));
    }

    const int committed_count = static_cast<int>(this->committed_entries.size());
    const int old_count = static_cast<int>(this->streaming_entries.size());
    const int new_count = static_cast<int>(non_empty_raw_markdown_blocks.size());
    const int shared_count = std::min(old_count, new_count);

    for (int index = 0; index < shared_count; ++index)
    {
        entry_t &entry = this->streaming_entries[index];
        entry.raw_markdown = non_empty_raw_markdown_blocks.at(index);
        entry.rendered_markdown = non_empty_rendered_markdown_blocks.at(index);
        entry.streaming = true;
        const QModelIndex row_index = this->index(committed_count + index, 0);
        emit this->dataChanged(row_index, row_index,
                               {Qt::DisplayRole, Qt::SizeHintRole, RAW_MARKDOWN_ROLE,
                                RENDERED_MARKDOWN_ROLE, STREAMING_ROLE});
    }

    if (new_count > old_count)
    {
        const int first_insert_row = committed_count + old_count;
        const int last_insert_row = committed_count + new_count - 1;
        this->beginInsertRows(QModelIndex(), first_insert_row, last_insert_row);
        for (int index = old_count; index < new_count; ++index)
        {
            this->streaming_entries.append(
                this->make_entry(non_empty_raw_markdown_blocks.at(index),
                                 non_empty_rendered_markdown_blocks.at(index), true));
        }
        this->endInsertRows();
    }
    else if (new_count < old_count)
    {
        const int first_remove_row = committed_count + new_count;
        const int last_remove_row = committed_count + old_count - 1;
        this->beginRemoveRows(QModelIndex(), first_remove_row, last_remove_row);
        while (this->streaming_entries.size() > new_count)
        {
            this->streaming_entries.removeLast();
        }
        this->endRemoveRows();
    }
}

void actions_log_model_t::clear_streaming_entries()
{
    this->set_streaming_entries({}, {});
}

void actions_log_model_t::commit_streaming_entries(const QStringList &raw_markdown_blocks,
                                                   const QStringList &rendered_markdown_blocks)
{
    Q_ASSERT(raw_markdown_blocks.size() == rendered_markdown_blocks.size());

    if (this->streaming_entries.isEmpty() == true)
    {
        for (qsizetype index = 0; index < raw_markdown_blocks.size(); ++index)
        {
            if (raw_markdown_blocks.at(index).trimmed().isEmpty() == true)
            {
                continue;
            }
            this->append_committed_entry(raw_markdown_blocks.at(index),
                                         rendered_markdown_blocks.at(index));
        }
        return;
    }

    this->set_streaming_entries(raw_markdown_blocks, rendered_markdown_blocks);
    if (this->streaming_entries.isEmpty() == true)
    {
        return;
    }

    this->beginResetModel();
    for (entry_t &entry : this->streaming_entries)
    {
        entry.streaming = false;
        this->committed_entries.append(entry);
    }
    this->streaming_entries.clear();
    this->endResetModel();
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
        if (index.isValid() == false || index.row() < 0 || index.row() >= this->rowCount())
        {
            continue;
        }
        blocks.append(this->data(index, RAW_MARKDOWN_ROLE).toString());
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
