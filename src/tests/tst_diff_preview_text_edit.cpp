#include "../src/diff/diff_preview_text_edit.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QtTest>

namespace qcai2
{

class tst_diff_preview_text_edit_t : public QObject
{
    Q_OBJECT

private slots:
    void ctrl_c_copies_selected_text();
};

void tst_diff_preview_text_edit_t::ctrl_c_copies_selected_text()
{
    diff_preview_text_edit_t preview(
        {QStringLiteral("- old line"), QStringLiteral("+ new line"), QStringLiteral(" context")});
    preview.show();
    QVERIFY(QTest::qWaitForWindowExposed(&preview));

    preview.setFocus();
    preview.selectAll();

    QGuiApplication::clipboard()->clear();
    QTest::keyClick(preview.viewport(), Qt::Key_C, Qt::ControlModifier);

    QCOMPARE(QGuiApplication::clipboard()->text(),
             QStringLiteral("- old line\n+ new line\n context"));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_diff_preview_text_edit_t)

#include "tst_diff_preview_text_edit.moc"
