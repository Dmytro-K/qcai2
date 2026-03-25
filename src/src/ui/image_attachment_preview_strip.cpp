/*! Implements the draft image-attachment preview strip shown under Linked Files. */

#include "image_attachment_preview_strip.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace qcai2
{

namespace
{

QPixmap preview_pixmap_for_path(const QString &absolute_path)
{
    QPixmap pixmap(absolute_path);
    if (pixmap.isNull() == true)
    {
        QPixmap fallback(120, 90);
        fallback.fill(Qt::transparent);
        return fallback;
    }

    return pixmap.scaled(120, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString display_name_for_attachment(const image_attachment_t &attachment)
{
    if (attachment.file_name.trimmed().isEmpty() == false)
    {
        return attachment.file_name.trimmed();
    }
    return QFileInfo(attachment.storage_path).fileName();
}

}  // namespace

image_attachment_preview_strip_t::image_attachment_preview_strip_t(QWidget *parent)
    : QWidget(parent)
{
    auto *outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);

    this->content_widget = new QWidget(this);
    auto *content_layout = new QHBoxLayout(this->content_widget);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(6);
    content_layout->addStretch(1);
    outer_layout->addWidget(this->content_widget);

    this->setVisible(false);
}

void image_attachment_preview_strip_t::set_attachments(
    const QList<image_attachment_t> &attachments, const QString &project_dir)
{
    this->attachments = attachments;
    this->project_dir = project_dir;
    this->rebuild();
}

QString image_attachment_preview_strip_t::absolute_path_for_attachment(
    const image_attachment_t &attachment) const
{
    const QFileInfo info(attachment.storage_path);
    if (info.isAbsolute() == true)
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }
    if (this->project_dir.isEmpty() == true)
    {
        return {};
    }
    return QDir(this->project_dir).absoluteFilePath(attachment.storage_path);
}

void image_attachment_preview_strip_t::rebuild()
{
    auto *content_layout = qobject_cast<QHBoxLayout *>(this->content_widget->layout());
    while (content_layout->count() > 0)
    {
        QLayoutItem *item = content_layout->takeAt(0);
        if (item->widget() != nullptr)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    for (const image_attachment_t &attachment : this->attachments)
    {
        auto *tile = new QWidget(this->content_widget);
        tile->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *tile_layout = new QVBoxLayout(tile);
        tile_layout->setContentsMargins(4, 4, 4, 4);
        tile_layout->setSpacing(2);

        auto *header_layout = new QHBoxLayout();
        header_layout->setContentsMargins(0, 0, 0, 0);
        header_layout->addStretch(1);

        auto *remove_button = new QPushButton(QStringLiteral("×"), tile);
        remove_button->setFixedSize(18, 18);
        remove_button->setToolTip(tr("Remove image attachment"));
        QObject::connect(remove_button, &QPushButton::clicked, tile, [this, attachment]() {
            emit this->remove_requested(attachment.attachment_id);
        });
        header_layout->addWidget(remove_button);
        tile_layout->addLayout(header_layout);

        auto *preview_button = new QToolButton(tile);
        preview_button->setAutoRaise(false);
        preview_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        preview_button->setIcon(
            QIcon(preview_pixmap_for_path(this->absolute_path_for_attachment(attachment))));
        preview_button->setIconSize(QSize(120, 90));
        preview_button->setFixedSize(128, 98);
        preview_button->setToolTip(display_name_for_attachment(attachment));
        QObject::connect(preview_button, &QToolButton::clicked, tile, [this, attachment]() {
            emit this->open_requested(attachment.attachment_id);
        });
        tile_layout->addWidget(preview_button);

        auto *name_label = new QLabel(display_name_for_attachment(attachment), tile);
        name_label->setAlignment(Qt::AlignHCenter);
        name_label->setWordWrap(true);
        name_label->setMaximumWidth(128);
        tile_layout->addWidget(name_label);

        content_layout->addWidget(tile);
    }

    content_layout->addStretch(1);
    this->setVisible(this->attachments.isEmpty() == false);
}

}  // namespace qcai2
