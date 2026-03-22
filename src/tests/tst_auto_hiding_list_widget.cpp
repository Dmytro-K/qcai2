#include "../src/ui/auto_hiding_list_widget.h"

#include <QtTest>

namespace qcai2
{

class tst_auto_hiding_list_widget_t : public QObject
{
    Q_OBJECT

private slots:
    void hides_when_empty();
    void shows_when_items_exist();
};

void tst_auto_hiding_list_widget_t::hides_when_empty()
{
    auto_hiding_list_widget_t widget;
    widget.addItem(QStringLiteral("temporary"));
    widget.clear();

    widget.refresh_visibility();

    QVERIFY(widget.isHidden());
}

void tst_auto_hiding_list_widget_t::shows_when_items_exist()
{
    auto_hiding_list_widget_t widget;
    widget.addItem(QStringLiteral("queued item"));

    widget.refresh_visibility();

    QVERIFY(widget.isVisible());
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_auto_hiding_list_widget_t)

#include "tst_auto_hiding_list_widget.moc"
