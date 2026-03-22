#include "../src/ui/request_duration_formatter.h"

#include <QtTest>

using namespace qcai2;

class tst_request_duration_formatter_t : public QObject
{
    Q_OBJECT

private slots:
    void formats_elapsed_seconds_with_flooring();
};

void tst_request_duration_formatter_t::formats_elapsed_seconds_with_flooring()
{
    QCOMPARE(format_request_duration_label(-1), QStringLiteral("0 s"));
    QCOMPARE(format_request_duration_label(0), QStringLiteral("0 s"));
    QCOMPARE(format_request_duration_label(999), QStringLiteral("0 s"));
    QCOMPARE(format_request_duration_label(1000), QStringLiteral("1 s"));
    QCOMPARE(format_request_duration_label(12'345), QStringLiteral("12 s"));
}

QTEST_APPLESS_MAIN(tst_request_duration_formatter_t)

#include "tst_request_duration_formatter.moc"
