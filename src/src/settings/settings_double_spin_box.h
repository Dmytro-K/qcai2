/*! @file
    @brief Declares a resettable floating-point settings widget with a square reset button.
*/
#pragma once

#include <QWidget>

#include <memory>

class QEvent;
class QResizeEvent;

namespace Ui
{
class settings_double_spin_box_t;
}

namespace qcai2
{

/**
 * Wraps one floating-point spin box together with a reset button that restores a configured
 * default.
 */
class settings_double_spin_box_t : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(double value READ value WRITE set_value NOTIFY value_changed)
    Q_PROPERTY(double defaultValue READ default_value WRITE set_default_value)
    Q_PROPERTY(double minimum READ minimum WRITE set_minimum)
    Q_PROPERTY(double maximum READ maximum WRITE set_maximum)
    Q_PROPERTY(double singleStep READ single_step WRITE set_single_step)
    Q_PROPERTY(int decimals READ decimals WRITE set_decimals)
    Q_PROPERTY(QString suffix READ suffix WRITE set_suffix)
    Q_PROPERTY(QString specialValueText READ special_value_text WRITE set_special_value_text)

public:
    /**
     * Creates one resettable floating-point settings widget.
     * @param parent Owning widget.
     */
    explicit settings_double_spin_box_t(QWidget *parent = nullptr);

    /** Destroys the widget and its generated UI. */
    ~settings_double_spin_box_t() override;

    /** Returns the current numeric value. */
    double value() const;

    /** Replaces the current numeric value. */
    void set_value(double value);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setValue(double value);

    /** Returns the configured reset/default value. */
    double default_value() const;

    /** Replaces the configured reset/default value. */
    void set_default_value(double value);

    /** Returns the minimum allowed value. */
    double minimum() const;

    /** Replaces the minimum allowed value. */
    void set_minimum(double value);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setMinimum(double value);

    /** Returns the maximum allowed value. */
    double maximum() const;

    /** Replaces the maximum allowed value. */
    void set_maximum(double value);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setMaximum(double value);

    /** Returns the step size used by the spin box. */
    double single_step() const;

    /** Replaces the step size used by the spin box. */
    void set_single_step(double value);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setSingleStep(double value);

    /** Returns the number of shown decimal places. */
    int decimals() const;

    /** Replaces the number of shown decimal places. */
    void set_decimals(int value);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setDecimals(int value);

    /** Returns the textual suffix shown after the numeric value. */
    QString suffix() const;

    /** Replaces the textual suffix shown after the numeric value. */
    void set_suffix(const QString &suffix);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setSuffix(const QString &suffix);

    /** Returns the text shown for the special minimum value, if any. */
    QString special_value_text() const;

    /** Replaces the text shown for the special minimum value, if any. */
    void set_special_value_text(const QString &text);

    /** Qt Designer compatibility wrapper for generated `uic` code. */
    void setSpecialValueText(const QString &text);

signals:
    /**
     * Emitted whenever the numeric value changes.
     * @param value New numeric value.
     */
    void value_changed(double value);

protected:
    /** Keeps the reset button square when the widget size changes. */
    void resizeEvent(QResizeEvent *event) override;

    /** Propagates tooltip/style changes to child controls. */
    bool event(QEvent *event) override;

private:
    /** Applies the configured default value to the spin box. */
    void reset_to_default();

    /** Enables or disables the reset button depending on the current value. */
    void update_reset_button_state();

    /** Keeps the reset button square and sized to the spin box height. */
    void update_reset_button_size();

    /** Applies current tooltips to the embedded controls. */
    void apply_tool_tips();

    /** Generated UI for the widget shell. */
    std::unique_ptr<Ui::settings_double_spin_box_t> ui;

    /** Default value restored by the reset button. */
    double configured_default_value = 0.0;
};

}  // namespace qcai2
