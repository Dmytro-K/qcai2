/*! @file
    @brief Declares the list model used by the Actions Log tab.
*/
#pragma once

#include <QAbstractListModel>
#include <QModelIndexList>
#include <QString>
#include <QStringList>

namespace qcai2
{

/**
 * Stores Actions Log entries as separate committed and optional trailing streaming rows.
 */
class actions_log_model_t final : public QAbstractListModel
{
    Q_OBJECT

public:
    /**
     * Extra roles exposed by the model for delegates and copy helpers.
     */
    enum role_t
    {
        RAW_MARKDOWN_ROLE = Qt::UserRole + 1,
        RENDERED_MARKDOWN_ROLE,
        ENTRY_ID_ROLE,
        STREAMING_ROLE
    };

    /**
     * Creates one Actions Log model.
     * @param parent Owning QObject.
     */
    explicit actions_log_model_t(QObject *parent = nullptr);

    /**
     * Returns the number of visible log rows.
     * @param parent Parent index; always invalid for this flat model.
     * @return Row count.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * Returns one data value for one Actions Log row.
     * @param index Requested row index.
     * @param role Requested data role.
     * @return Role-specific row data.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * Removes all committed and streaming rows.
     */
    void clear();

    /**
     * Replaces the committed rows with the provided markdown blocks.
     * @param raw_markdown_blocks Stored raw markdown blocks.
     * @param rendered_markdown_blocks Preprocessed markdown used for delegate rendering.
     */
    void set_committed_entries(const QStringList &raw_markdown_blocks,
                               const QStringList &rendered_markdown_blocks);

    /**
     * Appends one committed row to the end of the log.
     * @param raw_markdown Stored raw markdown block.
     * @param rendered_markdown Preprocessed markdown block for rendering.
     */
    void append_committed_entry(const QString &raw_markdown, const QString &rendered_markdown);

    /**
     * Replaces the trailing streaming preview rows.
     * @param raw_markdown_blocks Current streaming markdown previews in visual order.
     * @param rendered_markdown_blocks Preprocessed streaming markdown previews.
     */
    void set_streaming_entries(const QStringList &raw_markdown_blocks,
                               const QStringList &rendered_markdown_blocks);

    /**
     * Removes all trailing streaming preview rows.
     */
    void clear_streaming_entries();

    /**
     * Converts the trailing streaming rows into committed rows, or appends committed rows when no
     * preview rows exist.
     * @param raw_markdown_blocks Stored raw markdown blocks.
     * @param rendered_markdown_blocks Preprocessed markdown blocks for rendering.
     */
    void commit_streaming_entries(const QStringList &raw_markdown_blocks,
                                  const QStringList &rendered_markdown_blocks);

    /**
     * Joins the raw markdown of selected rows in visual order.
     * @param indexes Selected model indexes.
     * @return Joined raw markdown blocks.
     */
    QString selected_raw_markdown(const QModelIndexList &indexes) const;

private:
    /**
     * Stores one Actions Log row.
     */
    struct entry_t
    {
        QString entry_id;
        QString raw_markdown;
        QString rendered_markdown;
        bool streaming = false;
    };

    /**
     * Builds one new row object.
     * @param raw_markdown Stored raw markdown block.
     * @param rendered_markdown Preprocessed markdown block for rendering.
     * @param streaming True when the row is the temporary streaming preview.
     * @return New row object with a unique id.
     */
    entry_t make_entry(const QString &raw_markdown, const QString &rendered_markdown,
                       bool streaming) const;

    /** Stored committed Actions Log rows in visual order. */
    QList<entry_t> committed_entries;

    /** Temporary trailing streaming preview rows shown after committed entries. */
    QList<entry_t> streaming_entries;

    /** Monotonic counter used to generate stable row ids. */
    mutable qsizetype next_entry_id = 1;
};

}  // namespace qcai2
