/*! Covers persistent editor behavior in the Actions Log list view. */
#include "../src/ui/actions_log_delegate.h"
#include "../src/ui/actions_log_model.h"
#include "../src/ui/actions_log_view.h"

#include <QtTest>

namespace qcai2
{

class tst_actions_log_view_t : public QObject
{
    Q_OBJECT

private slots:
    void opens_persistent_editors_for_all_rows_without_current_selection();
    void relayouts_persistent_editors_after_row_data_grows();
};

void tst_actions_log_view_t::opens_persistent_editors_for_all_rows_without_current_selection()
{
    actions_log_model_t model;
    actions_log_delegate_t delegate;
    actions_log_view_t view;
    view.setItemDelegate(&delegate);
    view.setModel(&model);

    model.set_committed_entries({QStringLiteral("first"), QStringLiteral("second")},
                                {QStringLiteral("first"), QStringLiteral("second")});

    QCoreApplication::processEvents();

    QVERIFY(view.currentIndex().isValid() == false);
    QVERIFY(view.isPersistentEditorOpen(model.index(0, 0)));
    QVERIFY(view.isPersistentEditorOpen(model.index(1, 0)));
}

void tst_actions_log_view_t::relayouts_persistent_editors_after_row_data_grows()
{
    actions_log_model_t model;
    actions_log_delegate_t delegate;
    actions_log_view_t view;
    view.setItemDelegate(&delegate);
    view.setModel(&model);
    view.resize(260, 220);
    view.show();

    model.set_committed_entries({QStringLiteral("static row")}, {QStringLiteral("static row")});
    model.set_streaming_entries({QStringLiteral("short")}, {QStringLiteral("short")});

    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QCoreApplication::processEvents();

    const QModelIndex streaming_index = model.index(1, 0);
    const int initial_height = view.visualRect(streaming_index).height();

    const QString expanded_text =
        QStringLiteral("This is a much longer streaming Actions Log entry that should wrap across "
                       "multiple visual lines in the list view and therefore require a taller row "
                       "geometry than the initial short preview.");
    model.set_streaming_entries({expanded_text}, {expanded_text});
    QTRY_VERIFY(view.visualRect(streaming_index).height() > initial_height);
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_actions_log_view_t)

#include "tst_actions_log_view.moc"
