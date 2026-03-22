/*! Widget tests for the interactive structured decision-request card. */

#include "../src/ui/decision_request_widget.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QWidget>
#include <QtTest>

using namespace qcai2;

class tst_decision_request_widget_t : public QObject
{
    Q_OBJECT

private slots:
    void recommended_option_is_selected_initially();
    void missing_recommended_option_falls_back_to_first_choice();
    void empty_title_and_placeholder_use_defaults();
    void options_list_expands_to_fit_all_choices();
    void enter_submits_active_option_after_arrow_navigation();
    void click_submits_option_once();
    void freeform_return_submits_custom_answer();
    void freeform_button_click_submits_custom_answer();
    void freeform_only_request_hides_options_and_tracks_request_id();
    void request_without_freeform_hides_custom_answer_row();
    void empty_freeform_does_not_submit();
    void resolved_state_blocks_double_submit();
    void clear_request_resets_widget_state();
    void set_decision_request_replaces_previous_resolved_state();
};

namespace
{

agent_decision_request_t sample_request()
{
    agent_decision_request_t request;
    request.request_id = QStringLiteral("decision-1");
    request.title = QStringLiteral("Pick a validation strategy");
    request.description = QStringLiteral("Need your preference before continuing.");
    request.options = {agent_decision_option_t{QStringLiteral("fast"), QStringLiteral("Fast path"),
                                               QStringLiteral("Run targeted checks only")},
                       agent_decision_option_t{QStringLiteral("full"), QStringLiteral("Full path"),
                                               QStringLiteral("Run the whole suite")}};
    request.allow_freeform = true;
    request.freeform_placeholder = QStringLiteral("Describe another strategy");
    request.recommended_option_id = QStringLiteral("full");
    return request;
}

QListWidget *find_options_list(decision_request_widget_t &widget)
{
    return widget.findChild<QListWidget *>();
}

QLineEdit *find_freeform_edit(decision_request_widget_t &widget)
{
    return widget.findChild<QLineEdit *>();
}

QPushButton *find_submit_button(decision_request_widget_t &widget)
{
    return widget.findChild<QPushButton *>(QStringLiteral("decisionRequestSubmitButton"));
}

QLabel *find_resolved_label(decision_request_widget_t &widget)
{
    return widget.findChild<QLabel *>(QStringLiteral("decisionRequestResolvedLabel"));
}

QWidget *find_freeform_row(decision_request_widget_t &widget)
{
    return widget.findChild<QWidget *>(QStringLiteral("decisionRequestFreeformRow"));
}

}  // namespace

void tst_decision_request_widget_t::recommended_option_is_selected_initially()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QListWidget *options_list = find_options_list(widget);
    QVERIFY(options_list != nullptr);
    QCOMPARE(options_list->currentRow(), 1);
}

void tst_decision_request_widget_t::missing_recommended_option_falls_back_to_first_choice()
{
    decision_request_widget_t widget;
    widget.show();
    agent_decision_request_t request = sample_request();
    request.recommended_option_id = QStringLiteral("missing");
    widget.set_decision_request(request);

    QListWidget *options_list = find_options_list(widget);
    QVERIFY(options_list != nullptr);
    QCOMPARE(options_list->currentRow(), 0);
}

void tst_decision_request_widget_t::empty_title_and_placeholder_use_defaults()
{
    decision_request_widget_t widget;
    widget.show();
    agent_decision_request_t request;
    request.request_id = QStringLiteral("decision-defaults");
    request.options = {agent_decision_option_t{QStringLiteral("a"), QStringLiteral("Option A"),
                                               QStringLiteral("First")}};
    request.allow_freeform = true;
    widget.set_decision_request(request);

    QLabel *title_label = widget.findChild<QLabel *>(QStringLiteral("decisionRequestTitleLabel"));
    QLabel *description_label =
        widget.findChild<QLabel *>(QStringLiteral("decisionRequestDescriptionLabel"));
    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QVERIFY(title_label != nullptr);
    QVERIFY(description_label != nullptr);
    QVERIFY(freeform_edit != nullptr);
    QCOMPARE(title_label->text(), QStringLiteral("User decision required"));
    QVERIFY(description_label->isVisible() == false);
    QCOMPARE(freeform_edit->placeholderText(), QStringLiteral("Write your own answer"));
}

void tst_decision_request_widget_t::options_list_expands_to_fit_all_choices()
{
    decision_request_widget_t widget;
    widget.resize(320, 400);
    widget.show();

    agent_decision_request_t request;
    request.request_id = QStringLiteral("decision-sizing");
    request.title = QStringLiteral("Pick an approach");
    request.options = {
        agent_decision_option_t{QStringLiteral("a"), QStringLiteral("Short option"),
                                QStringLiteral("A longer description that should wrap in a narrow "
                                               "dock width and increase the item height.")},
        agent_decision_option_t{
            QStringLiteral("b"), QStringLiteral("Another option"),
            QStringLiteral("Another wrapped description to ensure the list grows tall enough for "
                           "every visible choice.")},
        agent_decision_option_t{QStringLiteral("c"), QStringLiteral("Third option"),
                                QStringLiteral("One more wrapped description for sizing.")}};
    widget.set_decision_request(request);
    QCoreApplication::processEvents();

    QListWidget *options_list = find_options_list(widget);
    QVERIFY(options_list != nullptr);

    int expected_height = options_list->frameWidth() * 2;
    for (int i = 0; i < options_list->count(); ++i)
    {
        expected_height += options_list->sizeHintForRow(i);
    }
    expected_height += qMax(0, options_list->count() - 1) * options_list->spacing();

    QCOMPARE(options_list->minimumHeight(), expected_height);
    QCOMPARE(options_list->maximumHeight(), expected_height);
    QVERIFY(options_list->verticalScrollBar()->isVisible() == false);
}

void tst_decision_request_widget_t::enter_submits_active_option_after_arrow_navigation()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QListWidget *options_list = find_options_list(widget);
    QVERIFY(options_list != nullptr);
    QSignalSpy option_spy(&widget, &decision_request_widget_t::option_submitted);

    options_list->setFocus();
    QTest::keyClick(options_list, Qt::Key_Up);
    QCOMPARE(options_list->currentRow(), 0);
    QTest::keyClick(options_list, Qt::Key_Return);

    QCOMPARE(option_spy.count(), 1);
    QCOMPARE(option_spy.at(0).at(0).toString(), QStringLiteral("decision-1"));
    QCOMPARE(option_spy.at(0).at(1).toString(), QStringLiteral("fast"));
}

void tst_decision_request_widget_t::click_submits_option_once()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QListWidget *options_list = find_options_list(widget);
    QVERIFY(options_list != nullptr);
    QSignalSpy option_spy(&widget, &decision_request_widget_t::option_submitted);

    const QRect item_rect = options_list->visualItemRect(options_list->item(1));
    QTest::mouseClick(options_list->viewport(), Qt::LeftButton, {}, item_rect.center());

    QCOMPARE(option_spy.count(), 1);
    QCOMPARE(option_spy.at(0).at(1).toString(), QStringLiteral("full"));
}

void tst_decision_request_widget_t::freeform_return_submits_custom_answer()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QVERIFY(freeform_edit != nullptr);
    QSignalSpy freeform_spy(&widget, &decision_request_widget_t::freeform_submitted);

    freeform_edit->setFocus();
    QTest::keyClicks(freeform_edit, QStringLiteral("Use a staged rollout"));
    QTest::keyClick(freeform_edit, Qt::Key_Return);

    QCOMPARE(freeform_spy.count(), 1);
    QCOMPARE(freeform_spy.at(0).at(0).toString(), QStringLiteral("decision-1"));
    QCOMPARE(freeform_spy.at(0).at(1).toString(), QStringLiteral("Use a staged rollout"));
}

void tst_decision_request_widget_t::freeform_button_click_submits_custom_answer()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QPushButton *submit_button = find_submit_button(widget);
    QVERIFY(freeform_edit != nullptr);
    QVERIFY(submit_button != nullptr);
    QSignalSpy freeform_spy(&widget, &decision_request_widget_t::freeform_submitted);

    freeform_edit->setText(QStringLiteral("Custom validation mix"));
    QTest::mouseClick(submit_button, Qt::LeftButton);

    QCOMPARE(freeform_spy.count(), 1);
    QCOMPARE(freeform_spy.at(0).at(1).toString(), QStringLiteral("Custom validation mix"));
}

void tst_decision_request_widget_t::freeform_only_request_hides_options_and_tracks_request_id()
{
    decision_request_widget_t widget;
    widget.show();

    agent_decision_request_t request;
    request.request_id = QStringLiteral("decision-freeform");
    request.title = QStringLiteral("Describe the approach");
    request.allow_freeform = true;
    request.freeform_placeholder = QStringLiteral("Write the approach");
    widget.set_decision_request(request);

    QListWidget *options_list = find_options_list(widget);
    QWidget *freeform_row = find_freeform_row(widget);
    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QVERIFY(options_list != nullptr);
    QVERIFY(freeform_row != nullptr);
    QVERIFY(freeform_edit != nullptr);
    QVERIFY(widget.has_request());
    QCOMPARE(widget.current_request_id(), QStringLiteral("decision-freeform"));
    QVERIFY(options_list->isVisible() == false);
    QVERIFY(freeform_row->isVisible());
    QCOMPARE(freeform_edit->placeholderText(), QStringLiteral("Write the approach"));
}

void tst_decision_request_widget_t::request_without_freeform_hides_custom_answer_row()
{
    decision_request_widget_t widget;
    widget.show();
    agent_decision_request_t request = sample_request();
    request.allow_freeform = false;
    widget.set_decision_request(request);

    QWidget *freeform_row = find_freeform_row(widget);
    QVERIFY(freeform_row != nullptr);
    QVERIFY(freeform_row->isVisible() == false);
}

void tst_decision_request_widget_t::empty_freeform_does_not_submit()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QVERIFY(freeform_edit != nullptr);
    QSignalSpy freeform_spy(&widget, &decision_request_widget_t::freeform_submitted);

    freeform_edit->setFocus();
    QTest::keyClick(freeform_edit, Qt::Key_Return);

    QCOMPARE(freeform_spy.count(), 0);
}

void tst_decision_request_widget_t::resolved_state_blocks_double_submit()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    QListWidget *options_list = find_options_list(widget);
    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QPushButton *submit_button = find_submit_button(widget);
    QLabel *resolved_label = find_resolved_label(widget);
    QVERIFY(options_list != nullptr);
    QVERIFY(freeform_edit != nullptr);
    QVERIFY(submit_button != nullptr);
    QVERIFY(resolved_label != nullptr);
    QSignalSpy option_spy(&widget, &decision_request_widget_t::option_submitted);
    QSignalSpy freeform_spy(&widget, &decision_request_widget_t::freeform_submitted);

    widget.mark_resolved(QStringLiteral("Full path"));
    options_list->setFocus();
    QTest::keyClick(options_list, Qt::Key_Return);
    freeform_edit->setText(QStringLiteral("Should not submit"));
    QTest::mouseClick(submit_button, Qt::LeftButton);

    QCOMPARE(option_spy.count(), 0);
    QCOMPARE(freeform_spy.count(), 0);
    QVERIFY(options_list->isEnabled() == false);
    QVERIFY(freeform_edit->isEnabled() == false);
    QVERIFY(submit_button->isEnabled() == false);
    QVERIFY(resolved_label->isVisible());
    QVERIFY(resolved_label->text().contains(QStringLiteral("Resolved: Full path")));
}

void tst_decision_request_widget_t::clear_request_resets_widget_state()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());

    widget.clear_request();

    QListWidget *options_list = find_options_list(widget);
    QLineEdit *freeform_edit = find_freeform_edit(widget);
    QLabel *resolved_label = find_resolved_label(widget);
    QVERIFY(options_list != nullptr);
    QVERIFY(freeform_edit != nullptr);
    QVERIFY(resolved_label != nullptr);
    QVERIFY(widget.has_request() == false);
    QCOMPARE(widget.current_request_id(), QString());
    QVERIFY(widget.isVisible() == false);
    QCOMPARE(options_list->count(), 0);
    QCOMPARE(freeform_edit->text(), QString());
    QVERIFY(resolved_label->isVisible() == false);
}

void tst_decision_request_widget_t::set_decision_request_replaces_previous_resolved_state()
{
    decision_request_widget_t widget;
    widget.show();
    widget.set_decision_request(sample_request());
    widget.mark_resolved(QStringLiteral("Fast path"));

    agent_decision_request_t replacement = sample_request();
    replacement.request_id = QStringLiteral("decision-2");
    replacement.title = QStringLiteral("Choose docs strategy");
    replacement.recommended_option_id.clear();
    widget.set_decision_request(replacement);

    QListWidget *options_list = find_options_list(widget);
    QLabel *resolved_label = find_resolved_label(widget);
    QVERIFY(options_list != nullptr);
    QVERIFY(resolved_label != nullptr);
    QVERIFY(widget.has_request());
    QCOMPARE(widget.current_request_id(), QStringLiteral("decision-2"));
    QVERIFY(resolved_label->isVisible() == false);
    QVERIFY(options_list->isEnabled());
    QCOMPARE(options_list->currentRow(), 0);
}

QTEST_MAIN(tst_decision_request_widget_t)

#include "tst_decision_request_widget.moc"
