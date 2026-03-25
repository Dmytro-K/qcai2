/*! Declares the draft image-attachment preview strip shown under Linked Files. */
#pragma once

#include "../models/agent_messages.h"

#include <QWidget>

namespace qcai2
{

/**
 * Renders draft image attachments as clickable thumbnails with remove buttons.
 */
class image_attachment_preview_strip_t : public QWidget
{
    Q_OBJECT

public:
    explicit image_attachment_preview_strip_t(QWidget *parent = nullptr);

    /**
     * Rebuilds the preview strip for the current attachment list.
     * @param attachments Attachments to show.
     * @param project_dir Active project directory used to resolve relative storage paths.
     */
    void set_attachments(const QList<image_attachment_t> &attachments, const QString &project_dir);

signals:
    /** Requests removal of one draft attachment from the dock state. */
    void remove_requested(const QString &attachment_id);

    /** Requests opening one attachment in the system or fallback viewer. */
    void open_requested(const QString &attachment_id);

private:
    void rebuild();
    QString absolute_path_for_attachment(const image_attachment_t &attachment) const;

    QList<image_attachment_t> attachments;
    QString project_dir;
    QWidget *content_widget = nullptr;
};

}  // namespace qcai2
