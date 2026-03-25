/*! @file
    @brief Implements the resettable floating-point settings widget.
*/

#include "settings_double_spin_box.h"

#include "../../qcai2tr.h"
#include "ui_settings_double_spin_box.h"

#include <QEvent>
#include <QResizeEvent>
#include <QStyle>

#include <cmath>

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

settings_double_spin_box_t::settings_double_spin_box_t(QWidget *parent)
    : QWidget(parent), ui(std::make_unique<Ui::settings_double_spin_box_t>())
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

    connect(this->ui->spin_box, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        this->update_reset_button_state();
        emit this->value_changed(value);
    });
    connect(this->ui->reset_button, &QToolButton::clicked, this,
            &settings_double_spin_box_t::reset_to_default);
}

settings_double_spin_box_t::~settings_double_spin_box_t() = default;

double settings_double_spin_box_t::value() const
{
    return this->ui->spin_box->value();
}

void settings_double_spin_box_t::set_value(double value)
{
    this->ui->spin_box->setValue(value);
    this->update_reset_button_state();
}

void settings_double_spin_box_t::setValue(double value)
{
    this->set_value(value);
}

double settings_double_spin_box_t::default_value() const
{
    return this->configured_default_value;
}

void settings_double_spin_box_t::set_default_value(double value)
{
    this->configured_default_value = value;
    this->update_reset_button_state();
}

double settings_double_spin_box_t::minimum() const
{
    return this->ui->spin_box->minimum();
}

void settings_double_spin_box_t::set_minimum(double value)
{
    this->ui->spin_box->setMinimum(value);
}

void settings_double_spin_box_t::setMinimum(double value)
{
    this->set_minimum(value);
}

double settings_double_spin_box_t::maximum() const
{
    return this->ui->spin_box->maximum();
}

void settings_double_spin_box_t::set_maximum(double value)
{
    this->ui->spin_box->setMaximum(value);
}

void settings_double_spin_box_t::setMaximum(double value)
{
    this->set_maximum(value);
}

double settings_double_spin_box_t::single_step() const
{
    return this->ui->spin_box->singleStep();
}

void settings_double_spin_box_t::set_single_step(double value)
{
    this->ui->spin_box->setSingleStep(value);
}

void settings_double_spin_box_t::setSingleStep(double value)
{
    this->set_single_step(value);
}

int settings_double_spin_box_t::decimals() const
{
    return this->ui->spin_box->decimals();
}

void settings_double_spin_box_t::set_decimals(int value)
{
    this->ui->spin_box->setDecimals(value);
}

void settings_double_spin_box_t::setDecimals(int value)
{
    this->set_decimals(value);
}

QString settings_double_spin_box_t::suffix() const
{
    return this->ui->spin_box->suffix();
}

void settings_double_spin_box_t::set_suffix(const QString &suffix)
{
    this->ui->spin_box->setSuffix(suffix);
}

void settings_double_spin_box_t::setSuffix(const QString &suffix)
{
    this->set_suffix(suffix);
}

QString settings_double_spin_box_t::special_value_text() const
{
    return this->ui->spin_box->specialValueText();
}

void settings_double_spin_box_t::set_special_value_text(const QString &text)
{
    this->ui->spin_box->setSpecialValueText(text);
}

void settings_double_spin_box_t::setSpecialValueText(const QString &text)
{
    this->set_special_value_text(text);
}

void settings_double_spin_box_t::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    this->update_reset_button_size();
}

bool settings_double_spin_box_t::event(QEvent *event)
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

void settings_double_spin_box_t::reset_to_default()
{
    this->set_value(this->configured_default_value);
}

void settings_double_spin_box_t::update_reset_button_state()
{
    const bool is_default =
        std::abs(this->ui->spin_box->value() - this->configured_default_value) <=
        this->ui->spin_box->singleStep() / 1000.0;
    this->ui->reset_button->setEnabled(is_default == false);
}

void settings_double_spin_box_t::update_reset_button_size()
{
    const int side = qMax(this->ui->spin_box->sizeHint().height(), this->ui->spin_box->height());
    this->ui->reset_button->setFixedSize(side, side);
    this->ui->reset_button->setIconSize(QSize(qMax(12, side - 10), qMax(12, side - 10)));
}

void settings_double_spin_box_t::apply_tool_tips()
{
    this->ui->spin_box->setToolTip(this->toolTip());
    this->ui->reset_button->setToolTip(tr_t::tr("Reset to default"));
}

}  // namespace qcai2
