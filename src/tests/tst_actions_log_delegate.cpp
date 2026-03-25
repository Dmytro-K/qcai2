#include "../src/ui/actions_log_delegate.h"
#include "../src/ui/actions_log_model.h"

#include <QListView>
#include <QTextBrowser>
#include <QtTest>

#include <memory>

namespace qcai2
{

class tst_actions_log_delegate_t : public QObject
{
    Q_OBJECT

private slots:
    void create_editor_returns_selectable_read_only_text_browser();
};

void tst_actions_log_delegate_t::create_editor_returns_selectable_read_only_text_browser()
{
    actions_log_model_t model;
    model.append_committed_entry(QStringLiteral("raw"), QStringLiteral("**Rendered** text"));

    QListView view;
    view.setModel(&model);

    actions_log_delegate_t delegate;
    const QModelIndex index = model.index(0, 0);

    QStyleOptionViewItem option;
    option.widget = &view;
    option.rect = QRect(0, 0, 320, 120);
    option.font = view.font();

    std::unique_ptr<QWidget> editor(delegate.createEditor(&view, option, index));
    auto *text_browser = qobject_cast<QTextBrowser *>(editor.get());
    QVERIFY(text_browser != nullptr);
    QVERIFY(text_browser->isReadOnly());
    QVERIFY((text_browser->textInteractionFlags() & Qt::TextSelectableByMouse) != 0);
    QVERIFY((text_browser->textInteractionFlags() & Qt::TextSelectableByKeyboard) != 0);

    delegate.setEditorData(text_browser, index);
    delegate.updateEditorGeometry(text_browser, option, index);

    QVERIFY(text_browser->toPlainText().contains(QStringLiteral("Rendered text")));
    QCOMPARE(text_browser->geometry(), option.rect.adjusted(8, 6, -8, -6));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_actions_log_delegate_t)

#include "tst_actions_log_delegate.moc"
