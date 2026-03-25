/*! Widget tests for resettable integer and floating-point settings spin boxes. */

#include "../src/settings/settings_double_spin_box.h"
#include "../src/settings/settings_spin_box.h"

#include <QSignalSpy>
#include <QSpinBox>
#include <QToolButton>
#include <QtTest>

using namespace qcai2;

class tst_settings_spin_box_widgets_t : public QObject
{
    Q_OBJECT

private slots:
    void int_widget_reset_button_tracks_default_state();
    void int_widget_reset_restores_default_value();
    void double_widget_reset_button_tracks_default_state();
    void double_widget_reset_restores_default_value();
};

namespace
{

QToolButton *find_reset_button(QWidget &widget)
{
    return widget.findChild<QToolButton *>(QStringLiteral("reset_button"));
}

QSpinBox *find_int_spin_box(settings_spin_box_t &widget)
{
    return widget.findChild<QSpinBox *>(QStringLiteral("spin_box"));
}

QDoubleSpinBox *find_double_spin_box(settings_double_spin_box_t &widget)
{
    return widget.findChild<QDoubleSpinBox *>(QStringLiteral("spin_box"));
}

}  // namespace

void tst_settings_spin_box_widgets_t::int_widget_reset_button_tracks_default_state()
{
    settings_spin_box_t widget;
    widget.set_default_value(25);
    widget.set_value(25);
    widget.show();

    QToolButton *reset_button = find_reset_button(widget);
    QVERIFY(reset_button != nullptr);
    QVERIFY(reset_button->isEnabled() == false);

    widget.set_value(30);
    QVERIFY(reset_button->isEnabled());
}

void tst_settings_spin_box_widgets_t::int_widget_reset_restores_default_value()
{
    settings_spin_box_t widget;
    QSpinBox *spin_box = find_int_spin_box(widget);
    QToolButton *reset_button = find_reset_button(widget);
    QVERIFY(reset_button != nullptr);
    QVERIFY(spin_box != nullptr);
    widget.set_default_value(25);
    widget.set_value(30);
    widget.show();

    QSignalSpy value_spy(&widget, &settings_spin_box_t::value_changed);
    QTest::mouseClick(reset_button, Qt::LeftButton);

    QCOMPARE(widget.value(), 25);
    QCOMPARE(spin_box->value(), 25);
    QCOMPARE(value_spy.count(), 1);
    QVERIFY(reset_button->isEnabled() == false);
}

void tst_settings_spin_box_widgets_t::double_widget_reset_button_tracks_default_state()
{
    settings_double_spin_box_t widget;
    widget.set_default_value(0.2);
    widget.set_value(0.2);
    widget.show();

    QToolButton *reset_button = find_reset_button(widget);
    QVERIFY(reset_button != nullptr);
    QVERIFY(reset_button->isEnabled() == false);

    widget.set_value(0.5);
    QVERIFY(reset_button->isEnabled());
}

void tst_settings_spin_box_widgets_t::double_widget_reset_restores_default_value()
{
    settings_double_spin_box_t widget;
    widget.set_default_value(0.2);
    widget.set_value(0.9);
    widget.show();

    QToolButton *reset_button = find_reset_button(widget);
    QDoubleSpinBox *spin_box = find_double_spin_box(widget);
    QVERIFY(reset_button != nullptr);
    QVERIFY(spin_box != nullptr);

    QSignalSpy value_spy(&widget, &settings_double_spin_box_t::value_changed);
    QTest::mouseClick(reset_button, Qt::LeftButton);

    QCOMPARE(widget.value(), 0.2);
    QCOMPARE(spin_box->value(), 0.2);
    QCOMPARE(value_spy.count(), 1);
    QVERIFY(reset_button->isEnabled() == false);
}

QTEST_MAIN(tst_settings_spin_box_widgets_t)
#include "tst_settings_spin_box_widgets.moc"
