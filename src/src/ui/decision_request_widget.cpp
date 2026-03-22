/*! Implements an interactive card widget for structured user-decision requests. */

#include "decision_request_widget.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace qcai2
{

decision_request_widget_t::decision_request_widget_t(QWidget *parent) : QFrame(parent)
{
    this->setObjectName(QStringLiteral("decisionRequestWidget"));
    this->setFrameShape(QFrame::StyledPanel);
    this->setFrameShadow(QFrame::Raised);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    this->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    this->title_label = new QLabel(this);
    this->title_label->setObjectName(QStringLiteral("decisionRequestTitleLabel"));
    this->title_label->setWordWrap(true);
    this->title_label->setStyleSheet(QStringLiteral("font-weight: bold;"));
    layout->addWidget(this->title_label);

    this->description_label = new QLabel(this);
    this->description_label->setObjectName(QStringLiteral("decisionRequestDescriptionLabel"));
    this->description_label->setWordWrap(true);
    layout->addWidget(this->description_label);

    this->options_list = new QListWidget(this);
    this->options_list->setObjectName(QStringLiteral("decisionRequestOptionsList"));
    this->options_list->setSelectionMode(QAbstractItemView::SingleSelection);
    this->options_list->setWordWrap(true);
    this->options_list->setAlternatingRowColors(true);
    this->options_list->setUniformItemSizes(false);
    this->options_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->options_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->options_list->installEventFilter(this);
    layout->addWidget(this->options_list);

    this->freeform_row = new QWidget(this);
    this->freeform_row->setObjectName(QStringLiteral("decisionRequestFreeformRow"));
    auto *freeform_layout = new QHBoxLayout(this->freeform_row);
    freeform_layout->setContentsMargins(0, 0, 0, 0);
    freeform_layout->setSpacing(4);
    this->freeform_edit = new QLineEdit(this->freeform_row);
    this->freeform_edit->setObjectName(QStringLiteral("decisionRequestFreeformEdit"));
    this->submit_freeform_button = new QPushButton(tr("Send"), this->freeform_row);
    this->submit_freeform_button->setObjectName(QStringLiteral("decisionRequestSubmitButton"));
    freeform_layout->addWidget(this->freeform_edit, 1);
    freeform_layout->addWidget(this->submit_freeform_button);
    layout->addWidget(this->freeform_row);

    this->resolved_label = new QLabel(this);
    this->resolved_label->setObjectName(QStringLiteral("decisionRequestResolvedLabel"));
    this->resolved_label->setWordWrap(true);
    this->resolved_label->setVisible(false);
    layout->addWidget(this->resolved_label);

    connect(this->options_list, &QListWidget::itemClicked, this,
            &decision_request_widget_t::submit_option_item);
    connect(this->options_list, &QListWidget::itemActivated, this,
            &decision_request_widget_t::submit_option_item);
    connect(this->freeform_edit, &QLineEdit::returnPressed, this,
            &decision_request_widget_t::submit_freeform);
    connect(this->submit_freeform_button, &QPushButton::clicked, this,
            &decision_request_widget_t::submit_freeform);
}

void decision_request_widget_t::set_decision_request(const agent_decision_request_t &request)
{
    this->current_request = request;
    this->has_active_request = true;

    this->title_label->setText(request.title.trimmed().isEmpty() == false
                                   ? request.title.trimmed()
                                   : QStringLiteral("User decision required"));
    this->description_label->setText(request.description.trimmed());
    this->description_label->setVisible(request.description.trimmed().isEmpty() == false);

    this->options_list->clear();
    int current_row = -1;
    for (int i = 0; i < request.options.size(); ++i)
    {
        const agent_decision_option_t &option = request.options.at(i);
        const QString item_text =
            option.description.trimmed().isEmpty() == false
                ? QStringLiteral("%1\n%2").arg(option.label, option.description)
                : option.label;
        auto *item = new QListWidgetItem(item_text, this->options_list);
        item->setData(Qt::UserRole, option.id);
        item->setToolTip(option.description);
        if (request.recommended_option_id.isEmpty() == false &&
            option.id == request.recommended_option_id)
        {
            current_row = i;
        }
    }

    this->options_list->setEnabled(true);
    this->options_list->setVisible(this->options_list->count() > 0);
    if (this->options_list->count() > 0)
    {
        if (current_row < 0)
        {
            current_row = 0;
        }
        this->options_list->setCurrentRow(current_row);
        this->options_list->setFocus(Qt::OtherFocusReason);
    }

    this->freeform_edit->clear();
    this->freeform_edit->setEnabled(true);
    this->freeform_edit->setPlaceholderText(request.freeform_placeholder.trimmed().isEmpty() ==
                                                    false
                                                ? request.freeform_placeholder.trimmed()
                                                : tr("Write your own answer"));
    this->submit_freeform_button->setEnabled(true);
    this->freeform_row->setVisible(request.allow_freeform);

    this->resolved_label->clear();
    this->resolved_label->setVisible(false);
    this->setEnabled(true);
    this->setVisible(true);
    this->update_options_list_height();
    this->updateGeometry();

    if (this->options_list->count() == 0 && request.allow_freeform == true)
    {
        this->freeform_edit->setFocus(Qt::OtherFocusReason);
    }
}

void decision_request_widget_t::mark_resolved(const QString &answer_summary)
{
    this->options_list->setEnabled(false);
    this->freeform_edit->setEnabled(false);
    this->submit_freeform_button->setEnabled(false);
    this->resolved_label->setText(
        answer_summary.trimmed().isEmpty() == false
            ? QStringLiteral("Resolved: %1").arg(answer_summary.trimmed())
            : QStringLiteral("Resolved."));
    this->resolved_label->setVisible(true);
}

void decision_request_widget_t::clear_request()
{
    this->current_request = {};
    this->has_active_request = false;
    this->title_label->clear();
    this->description_label->clear();
    this->description_label->setVisible(false);
    this->options_list->clear();
    this->options_list->setMinimumHeight(0);
    this->options_list->setMaximumHeight(QWIDGETSIZE_MAX);
    this->options_list->setVisible(false);
    this->freeform_edit->clear();
    this->freeform_row->setVisible(false);
    this->resolved_label->clear();
    this->resolved_label->setVisible(false);
    this->setVisible(false);
}

bool decision_request_widget_t::has_request() const
{
    return this->has_active_request;
}

QString decision_request_widget_t::current_request_id() const
{
    return this->has_active_request == true ? this->current_request.request_id : QString();
}

bool decision_request_widget_t::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->options_list && event != nullptr && event->type() == QEvent::KeyPress)
    {
        auto *key_event = static_cast<QKeyEvent *>(event);
        if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter)
        {
            this->submit_option_item(this->options_list->currentItem());
            return true;
        }
    }

    return QFrame::eventFilter(watched, event);
}

void decision_request_widget_t::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    this->update_options_list_height();
}

void decision_request_widget_t::update_options_list_height()
{
    if (this->options_list->count() == 0 || this->options_list->isVisible() == false)
    {
        this->options_list->setMinimumHeight(0);
        this->options_list->setMaximumHeight(QWIDGETSIZE_MAX);
        return;
    }

    this->options_list->doItemsLayout();

    int content_height = this->options_list->frameWidth() * 2;
    for (int i = 0; i < this->options_list->count(); ++i)
    {
        const int row_height = this->options_list->sizeHintForRow(i);
        content_height += row_height > 0 ? row_height : this->fontMetrics().lineSpacing() * 2;
    }

    content_height += qMax(0, this->options_list->count() - 1) * this->options_list->spacing();
    if (this->options_list->horizontalScrollBar()->isVisible() == true)
    {
        content_height += this->options_list->horizontalScrollBar()->sizeHint().height();
    }

    this->options_list->setMinimumHeight(content_height);
    this->options_list->setMaximumHeight(content_height);
}

void decision_request_widget_t::submit_option_item(QListWidgetItem *item)
{
    if (this->has_active_request == false || item == nullptr ||
        this->options_list->isEnabled() == false)
    {
        return;
    }

    const QString option_id = item->data(Qt::UserRole).toString();
    if (option_id.isEmpty() == true)
    {
        return;
    }

    emit this->option_submitted(this->current_request.request_id, option_id);
}

void decision_request_widget_t::submit_freeform()
{
    if (this->has_active_request == false || this->freeform_edit->isEnabled() == false)
    {
        return;
    }

    const QString text = this->freeform_edit->text().trimmed();
    if (text.isEmpty() == true)
    {
        return;
    }

    emit this->freeform_submitted(this->current_request.request_id, text);
}

}  // namespace qcai2
