#include "../src/debugger/debugger_session_service.h"
#include "../src/ui/debugger_status_widget.h"

#include <QtTest>

using namespace qcai2;

namespace
{

QLabel *named_label(debugger_status_widget_t &widget, const QString &name)
{
    return widget.findChild<QLabel *>(name);
}

QPlainTextEdit *output_view(debugger_status_widget_t &widget)
{
    return widget.findChild<QPlainTextEdit *>(QStringLiteral("debugger_output_view"));
}

class fake_debugger_service_ui_t final : public i_debugger_session_service_t
{
    Q_OBJECT

public:
    explicit fake_debugger_service_ui_t(QObject *parent = nullptr)
        : i_debugger_session_service_t(parent)
    {
    }

    debugger_session_snapshot_t snapshot_value;
    QString output = QStringLiteral("line a\nline b");
    QList<debugger_managed_breakpoint_t> managed;

    bool has_active_session() const override
    {
        return snapshot_value.active;
    }
    bool is_paused() const override
    {
        return snapshot_value.paused;
    }
    debugger_session_snapshot_t session_snapshot() const override
    {
        return snapshot_value;
    }
    QList<debugger_stack_frame_t> stack_frames(int) const override
    {
        return {};
    }
    QList<debugger_thread_t> threads(int) const override
    {
        return {};
    }
    QList<debugger_variable_t> locals(int, int) const override
    {
        return {};
    }
    QList<debugger_variable_t> watch_expressions(int, int) const override
    {
        return {};
    }
    QList<debugger_breakpoint_t> breakpoints(int) const override
    {
        return {};
    }
    QString debugger_output_snapshot(int) const override
    {
        return output;
    }
    QString debugger_diagnostics_snapshot(int) const override
    {
        return QString();
    }
    debugger_action_result_t start_debugger() override
    {
        return {};
    }
    debugger_action_result_t continue_execution() override
    {
        return {};
    }
    debugger_action_result_t step_over() override
    {
        return {};
    }
    debugger_action_result_t step_into() override
    {
        return {};
    }
    debugger_action_result_t step_out() override
    {
        return {};
    }
    debugger_action_result_t stop_debugger() override
    {
        return {};
    }
    debugger_action_result_t set_managed_breakpoint(const debugger_managed_breakpoint_t &) override
    {
        return {};
    }
    debugger_action_result_t
    remove_managed_breakpoint(const debugger_managed_breakpoint_t &) override
    {
        return {};
    }
    debugger_action_result_t set_managed_breakpoint_enabled(const debugger_managed_breakpoint_t &,
                                                            bool) override
    {
        return {};
    }
    QList<debugger_managed_breakpoint_t> managed_breakpoints() const override
    {
        return managed;
    }
    void set_managed_breakpoints(const QList<debugger_managed_breakpoint_t> &breakpoints) override
    {
        managed = breakpoints;
        emit this->managed_breakpoints_changed();
    }

    void emit_snapshot_update()
    {
        emit this->snapshot_changed();
    }
    void emit_output_update()
    {
        emit this->debugger_output_changed();
    }
};

}  // namespace

class tst_debugger_ui_integration_t : public QObject
{
    Q_OBJECT

private slots:
    void widget_handles_null_service();
    void widget_shows_inactive_message();
    void widget_refreshes_active_summary();
    void widget_reacts_to_service_signals();
};

void tst_debugger_ui_integration_t::widget_handles_null_service()
{
    debugger_status_widget_t widget(nullptr);
    QCOMPARE(named_label(widget, QStringLiteral("session_summary_label"))->text(),
             QStringLiteral("Debugger integration is not available."));
    QVERIFY(named_label(widget, QStringLiteral("frame_summary_label"))->text().isEmpty());
    QVERIFY(named_label(widget, QStringLiteral("breakpoint_summary_label"))->text().isEmpty());
    QVERIFY(output_view(widget)->toPlainText().isEmpty());
}

void tst_debugger_ui_integration_t::widget_shows_inactive_message()
{
    fake_debugger_service_ui_t service;
    debugger_status_widget_t widget(&service);
    QCOMPARE(named_label(widget, QStringLiteral("session_summary_label"))->text(),
             QStringLiteral("No active debugger session."));
    QVERIFY(named_label(widget, QStringLiteral("frame_summary_label"))
                ->text()
                .contains(QStringLiteral("Start a Qt Creator debug session")));
    QVERIFY(named_label(widget, QStringLiteral("breakpoint_summary_label"))
                ->text()
                .contains(QStringLiteral("Managed breakpoints: 0")));
    QVERIFY(output_view(widget)->toPlainText().contains(QStringLiteral("line a")));
}

void tst_debugger_ui_integration_t::widget_refreshes_active_summary()
{
    fake_debugger_service_ui_t service;
    service.snapshot_value.active = true;
    service.snapshot_value.paused = true;
    service.snapshot_value.engine_display_name = QStringLiteral("GDB");
    service.snapshot_value.state_name = QStringLiteral("InferiorStopOk");
    service.snapshot_value.location.file_path = QStringLiteral("/tmp/main.cpp");
    service.snapshot_value.location.line = 9;
    service.snapshot_value.location.function_name = QStringLiteral("main");
    service.snapshot_value.breakpoint_count = 2;
    debugger_managed_breakpoint_t managed_breakpoint;
    managed_breakpoint.file_path = QStringLiteral("/tmp/main.cpp");
    managed_breakpoint.line = 9;
    service.managed.append(managed_breakpoint);
    debugger_status_widget_t widget(&service);
    widget.refresh();
    QVERIFY(named_label(widget, QStringLiteral("session_summary_label"))
                ->text()
                .contains(QStringLiteral("GDB")));
    QVERIFY(named_label(widget, QStringLiteral("session_summary_label"))
                ->text()
                .contains(QStringLiteral("paused")));
    QVERIFY(named_label(widget, QStringLiteral("frame_summary_label"))
                ->text()
                .contains(QStringLiteral("/tmp/main.cpp:9")));
    QVERIFY(named_label(widget, QStringLiteral("breakpoint_summary_label"))
                ->text()
                .contains(QStringLiteral("2 total | 1 managed")));
}

void tst_debugger_ui_integration_t::widget_reacts_to_service_signals()
{
    fake_debugger_service_ui_t service;
    debugger_status_widget_t widget(&service);

    service.snapshot_value.active = true;
    service.snapshot_value.paused = false;
    service.snapshot_value.engine_display_name = QStringLiteral("LLDB");
    service.snapshot_value.state_name = QStringLiteral("InferiorRunOk");
    service.snapshot_value.location.file_path = QStringLiteral("/tmp/updated.cpp");
    service.snapshot_value.location.line = 21;
    service.snapshot_value.location.function_name = QStringLiteral("worker");
    service.snapshot_value.breakpoint_count = 7;
    service.output = QStringLiteral("updated output");
    debugger_managed_breakpoint_t managed_breakpoint;
    managed_breakpoint.file_path = QStringLiteral("/tmp/updated.cpp");
    managed_breakpoint.line = 21;
    service.set_managed_breakpoints({managed_breakpoint});
    service.emit_snapshot_update();
    service.emit_output_update();
    QCoreApplication::processEvents();

    QVERIFY(named_label(widget, QStringLiteral("session_summary_label"))
                ->text()
                .contains(QStringLiteral("LLDB")));
    QVERIFY(named_label(widget, QStringLiteral("session_summary_label"))
                ->text()
                .contains(QStringLiteral("running")));
    QVERIFY(named_label(widget, QStringLiteral("frame_summary_label"))
                ->text()
                .contains(QStringLiteral("/tmp/updated.cpp:21")));
    QVERIFY(named_label(widget, QStringLiteral("breakpoint_summary_label"))
                ->text()
                .contains(QStringLiteral("7 total | 1 managed")));
    QCOMPARE(output_view(widget)->toPlainText(), QStringLiteral("updated output"));
}

QTEST_MAIN(tst_debugger_ui_integration_t)

#include "tst_debugger_ui_integration.moc"
