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

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_actions_log_view_t)

#include "tst_actions_log_view.moc"
