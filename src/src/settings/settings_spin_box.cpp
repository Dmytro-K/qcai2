/*! @file
    @brief Implements the resettable integer settings widget.
*/

#include "settings_spin_box.h"

#include "../../qcai2tr.h"
#include "ui_settings_spin_box.h"

#include <QEvent>
#include <QResizeEvent>
#include <QStyle>

namespace qcai2
{

namespace
{

QIcon reset_icon(QWidget *widget)
{
    return QIcon::fromTheme(QStringLiteral("view-refresh"),
                            widget->style()->standardIcon(QStyle::SP_BrowserReload));
}

}  // namespace

settings_spin_box_t::settings_spin_box_t(QWidget *parent)
    : QWidget(parent), ui(std::make_unique<Ui::settings_spin_box_t>())
{
    this->ui->setupUi(this);
    this->configured_default_value = this->ui->spin_box->value();
    this->setFocusProxy(this->ui->spin_box);
    this->ui->reset_button->setIcon(reset_icon(this));
    this->ui->reset_button->setFocusPolicy(Qt::NoFocus);
    this->ui->reset_button->setAutoRaise(true);
    this->apply_tool_tips();
    this->update_reset_button_size();
    this->update_reset_button_state();

    connect(this->ui->spin_box, &QSpinBox::valueChanged, this, [this](int value) {
        this->update_reset_button_state();
        emit this->value_changed(value);
    });
    connect(this->ui->reset_button, &QToolButton::clicked, this,
            &settings_spin_box_t::reset_to_default);
}

settings_spin_box_t::~settings_spin_box_t() = default;

int settings_spin_box_t::value() const
{
    return this->ui->spin_box->value();
}

void settings_spin_box_t::set_value(int value)
{
    this->ui->spin_box->setValue(value);
    this->update_reset_button_state();
}

void settings_spin_box_t::setValue(int value)
{
    this->set_value(value);
}

int settings_spin_box_t::default_value() const
{
    return this->configured_default_value;
}

void settings_spin_box_t::set_default_value(int value)
{
    this->configured_default_value = value;
    this->update_reset_button_state();
}

int settings_spin_box_t::minimum() const
{
    return this->ui->spin_box->minimum();
}

void settings_spin_box_t::set_minimum(int value)
{
    this->ui->spin_box->setMinimum(value);
}

void settings_spin_box_t::setMinimum(int value)
{
    this->set_minimum(value);
}

int settings_spin_box_t::maximum() const
{
    return this->ui->spin_box->maximum();
}

void settings_spin_box_t::set_maximum(int value)
{
    this->ui->spin_box->setMaximum(value);
}

void settings_spin_box_t::setMaximum(int value)
{
    this->set_maximum(value);
}

int settings_spin_box_t::single_step() const
{
    return this->ui->spin_box->singleStep();
}

void settings_spin_box_t::set_single_step(int value)
{
    this->ui->spin_box->setSingleStep(value);
}

void settings_spin_box_t::setSingleStep(int value)
{
    this->set_single_step(value);
}

QString settings_spin_box_t::suffix() const
{
    return this->ui->spin_box->suffix();
}

void settings_spin_box_t::set_suffix(const QString &suffix)
{
    this->ui->spin_box->setSuffix(suffix);
}

void settings_spin_box_t::setSuffix(const QString &suffix)
{
    this->set_suffix(suffix);
}

QString settings_spin_box_t::special_value_text() const
{
    return this->ui->spin_box->specialValueText();
}

void settings_spin_box_t::set_special_value_text(const QString &text)
{
    this->ui->spin_box->setSpecialValueText(text);
}

void settings_spin_box_t::setSpecialValueText(const QString &text)
{
    this->set_special_value_text(text);
}

void settings_spin_box_t::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    this->update_reset_button_size();
}

bool settings_spin_box_t::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (event != nullptr)
    {
        if (event->type() == QEvent::ToolTipChange)
        {
            this->apply_tool_tips();
        }
        else if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange ||
                 event->type() == QEvent::Polish)
        {
            this->ui->reset_button->setIcon(reset_icon(this));
            this->update_reset_button_size();
            this->apply_tool_tips();
        }
    }
    return handled;
}

void settings_spin_box_t::reset_to_default()
{
    this->set_value(this->configured_default_value);
}

void settings_spin_box_t::update_reset_button_state()
{
    this->ui->reset_button->setEnabled(this->ui->spin_box->value() !=
                                       this->configured_default_value);
}

void settings_spin_box_t::update_reset_button_size()
{
    const int side = qMax(this->ui->spin_box->sizeHint().height(), this->ui->spin_box->height());
    this->ui->reset_button->setFixedSize(side, side);
    this->ui->reset_button->setIconSize(QSize(qMax(12, side - 10), qMax(12, side - 10)));
}

void settings_spin_box_t::apply_tool_tips()
{
    this->ui->spin_box->setToolTip(this->toolTip());
    this->ui->reset_button->setToolTip(tr_t::tr("Reset to default"));
}

}  // namespace qcai2
