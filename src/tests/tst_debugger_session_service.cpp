#include "../src/debugger/debugger_session_service.h"

#include <debugger/breakhandler.h>
#include <debugger/debuggerconstants.h>
#include <debugger/debuggerinternalconstants.h>

#include <QAction>
#include <QStandardItemModel>
#include <QtTest>

#include <optional>

using namespace qcai2;

namespace
{

class runtime_hooks_guard_t
{
public:
    explicit runtime_hooks_guard_t(const debugger_runtime_hooks_t &hooks)
    {
        debugger_session_service_t::set_runtime_hooks_for_testing(hooks);
    }

    ~runtime_hooks_guard_t()
    {
        debugger_session_service_t::reset_runtime_hooks_for_testing();
    }
};

QString breakpoint_key(const debugger_managed_breakpoint_t &breakpoint)
{
    return QStringLiteral("%1:%2|%3")
        .arg(breakpoint.file_path)
        .arg(breakpoint.line)
        .arg(breakpoint.function_name);
}

void set_breakpoint_row(QStandardItemModel &model, int row, const QString &display_name,
                        const QString &function_name, const QString &file_path,
                        const QVariant &line_value, const QString &condition,
                        std::optional<Qt::CheckState> check_state = std::nullopt)
{
    model.setColumnCount(Debugger::Internal::BreakpointConditionColumn + 1);
    if (row >= model.rowCount())
    {
        model.setRowCount(row + 1);
    }

    model.setData(model.index(row, Debugger::Internal::BreakpointNumberColumn), display_name);
    model.setData(model.index(row, Debugger::Internal::BreakpointFunctionColumn), function_name);
    model.setData(model.index(row, Debugger::Internal::BreakpointFileColumn), file_path);
    model.setData(model.index(row, Debugger::Internal::BreakpointLineColumn), line_value);
    model.setData(model.index(row, Debugger::Internal::BreakpointConditionColumn), condition);
    if (check_state.has_value())
    {
        model.setData(model.index(row, Debugger::Internal::BreakpointNumberColumn),
                      static_cast<int>(*check_state), Qt::CheckStateRole);
    }
}

int append_breakpoint_row(QStandardItemModel &model, const QString &display_name,
                          const QString &function_name, const QString &file_path,
                          const QVariant &line_value, const QString &condition,
                          std::optional<Qt::CheckState> check_state = std::nullopt)
{
    const int row = model.rowCount();
    set_breakpoint_row(model, row, display_name, function_name, file_path, line_value, condition,
                       check_state);
    return row;
}

int find_breakpoint_row(const QStandardItemModel &model, const QString &file_path, int line)
{
    for (int row = 0; row < model.rowCount(); ++row)
    {
        if (model.index(row, Debugger::Internal::BreakpointFileColumn).data().toString() ==
                file_path &&
            model.index(row, Debugger::Internal::BreakpointLineColumn).data().toInt() == line)
        {
            return row;
        }
    }

    return -1;
}

}  // namespace

class tst_debugger_session_service_t : public QObject
{
    Q_OBJECT

private slots:
    void managed_breakpoint_roundtrip();
    void empty_service_reports_inactive_state();
    void testing_output_capture_produces_diagnostics();
    void serialization_helpers_include_expected_fields();
    void empty_service_queries_and_actions_return_safe_defaults();
    void managed_breakpoint_lifecycle_and_signal_emission();
    void set_managed_breakpoints_filters_invalid_entries();
    void session_snapshot_tracks_hooked_runtime_states();
    void breakpoint_snapshot_reads_model_rows_and_limits();
    void control_actions_cover_success_disabled_and_unavailable();
    void runtime_mutation_hooks_cover_sync_and_remove_paths();
};

void tst_debugger_session_service_t::managed_breakpoint_roundtrip()
{
    debugger_managed_breakpoint_t breakpoint;
    breakpoint.file_path = QStringLiteral("/tmp/project/main.cpp");
    breakpoint.line = 42;
    breakpoint.condition = QStringLiteral("i > 0");
    breakpoint.enabled = false;

    QVERIFY(breakpoint.is_valid());
    const debugger_managed_breakpoint_t restored =
        debugger_managed_breakpoint_t::from_json(breakpoint.to_json());
    QCOMPARE(restored.file_path, breakpoint.file_path);
    QCOMPARE(restored.line, breakpoint.line);
    QCOMPARE(restored.condition, breakpoint.condition);
    QCOMPARE(restored.enabled, breakpoint.enabled);
}

void tst_debugger_session_service_t::empty_service_reports_inactive_state()
{
    debugger_session_service_t service;
    QVERIFY(!service.has_active_session());
    QVERIFY(!service.is_paused());
    const debugger_session_snapshot_t snapshot = service.session_snapshot();
    QVERIFY(!snapshot.active);
    QVERIFY(snapshot.state_name.contains(QStringLiteral("No active")));
    QCOMPARE(service.debugger_output_snapshot(50),
             QStringLiteral("No debugger output captured yet."));
}

void tst_debugger_session_service_t::testing_output_capture_produces_diagnostics()
{
    debugger_session_service_t service;
    QSignalSpy output_spy(&service, &i_debugger_session_service_t::debugger_output_changed);
    service.append_debugger_output_for_testing(
        QStringLiteral("/tmp/project/main.cpp:17: error: something broke"));
    service.append_debugger_output_for_testing(QStringLiteral("tail line"));
    QVERIFY(output_spy.count() >= 1);
    QVERIFY(service.debugger_output_snapshot(10).contains(QStringLiteral("something broke")));
    QVERIFY(!service.debugger_output_snapshot(1).contains(QStringLiteral("something broke")));
    QVERIFY(service.debugger_output_snapshot(1).contains(QStringLiteral("tail line")));
    QVERIFY(service.debugger_diagnostics_snapshot(10).contains(QStringLiteral("[error]")));
}

void tst_debugger_session_service_t::serialization_helpers_include_expected_fields()
{
    debugger_location_t location{QStringLiteral("/tmp/main.cpp"), 7, QStringLiteral("main"),
                                 QStringLiteral("app"), QStringLiteral("0x1234")};
    debugger_stack_frame_t frame{1,   QStringLiteral("func"), QStringLiteral("/tmp/frame.cpp"),
                                 9,   QStringLiteral("mod"),  QStringLiteral("0xab"),
                                 true};
    debugger_thread_t thread{QStringLiteral("t1"),
                             QStringLiteral("worker"),
                             QStringLiteral("stopped"),
                             QStringLiteral("run"),
                             QStringLiteral("/tmp/thread.cpp"),
                             11,
                             QStringLiteral("details"),
                             true};
    debugger_variable_t variable{QStringLiteral("local.i"),
                                 QStringLiteral("i"),
                                 QStringLiteral("i"),
                                 QStringLiteral("42"),
                                 QStringLiteral("int"),
                                 true,
                                 false,
                                 2};
    debugger_breakpoint_t breakpoint{5,
                                     QStringLiteral("/tmp/bp.cpp"),
                                     13,
                                     QStringLiteral("f"),
                                     QStringLiteral("i > 0"),
                                     false,
                                     true,
                                     QStringLiteral("bp#5")};
    debugger_session_snapshot_t snapshot;
    snapshot.active = true;
    snapshot.paused = true;
    snapshot.state_name = QStringLiteral("Paused");
    snapshot.engine_display_name = QStringLiteral("Qt Creator debugger");
    snapshot.engine_type = QStringLiteral("qtcreator-actions");
    snapshot.location = location;
    snapshot.stack_frame_count = 2;
    snapshot.thread_count = 3;
    snapshot.variable_count = 4;
    snapshot.breakpoint_count = 5;
    debugger_action_result_t action_result{true, QStringLiteral("done")};

    QCOMPARE(location.to_json().value(QStringLiteral("file_path")).toString(), location.file_path);
    QCOMPARE(frame.to_json().value(QStringLiteral("current")).toBool(), true);
    QCOMPARE(thread.to_json().value(QStringLiteral("details")).toString(), thread.details);
    QCOMPARE(variable.to_json().value(QStringLiteral("type")).toString(), variable.type);
    QCOMPARE(breakpoint.to_json().value(QStringLiteral("display_name")).toString(),
             breakpoint.display_name);
    const QString snapshot_json = format_debugger_snapshot_json(snapshot);
    QVERIFY(snapshot_json.contains(QStringLiteral("Qt Creator debugger")));
    QVERIFY(snapshot_json.contains(QStringLiteral("/tmp/main.cpp")));
    const QString action_json = format_debugger_action_result_json(action_result);
    QVERIFY(action_json.contains(QStringLiteral("\"success\": true")));
    QVERIFY(action_json.contains(QStringLiteral("done")));
}

void tst_debugger_session_service_t::empty_service_queries_and_actions_return_safe_defaults()
{
    debugger_session_service_t service;
    QVERIFY(service.stack_frames().isEmpty());
    QVERIFY(service.threads().isEmpty());
    QVERIFY(service.locals().isEmpty());
    QVERIFY(service.watch_expressions().isEmpty());
    QVERIFY(service.breakpoints().isEmpty());
    QCOMPARE(service.debugger_diagnostics_snapshot(5), QStringLiteral("No diagnostics found."));

    service.append_debugger_output_for_testing(QStringLiteral("partial"), false);
    QCOMPARE(service.debugger_output_snapshot(5), QStringLiteral("partial"));
    service.append_debugger_output_for_testing(QStringLiteral(" tail"), false);
    QCOMPARE(service.debugger_output_snapshot(5), QStringLiteral("partial tail"));

    const QList<debugger_action_result_t> action_results{
        service.continue_execution(), service.step_over(), service.step_into(), service.step_out(),
        service.stop_debugger()};
    for (const debugger_action_result_t &result : action_results)
    {
        QVERIFY(!result.success);
        QVERIFY(result.message.contains(QStringLiteral("unavailable")));
    }
}

void tst_debugger_session_service_t::managed_breakpoint_lifecycle_and_signal_emission()
{
    debugger_session_service_t service;
    QSignalSpy managed_spy(&service, &i_debugger_session_service_t::managed_breakpoints_changed);
    QSignalSpy snapshot_spy(&service, &i_debugger_session_service_t::snapshot_changed);

    const debugger_action_result_t invalid_added = service.set_managed_breakpoint({});
    QVERIFY(!invalid_added.success);
    QVERIFY(invalid_added.message.contains(QStringLiteral("requires file+line or function_name")));

    debugger_managed_breakpoint_t breakpoint;
    breakpoint.file_path = QStringLiteral("/tmp/project/main.cpp");
    breakpoint.line = 17;
    breakpoint.condition = QStringLiteral("i > 0");

    const debugger_action_result_t added = service.set_managed_breakpoint(breakpoint);
    QVERIFY(added.success);
    QCOMPARE(service.managed_breakpoints().size(), 1);
    QVERIFY(managed_spy.count() >= 1);
    QVERIFY(snapshot_spy.count() >= 1);

    const debugger_action_result_t duplicate = service.set_managed_breakpoint(breakpoint);
    QVERIFY(!duplicate.success);
    QVERIFY(duplicate.message.contains(QStringLiteral("already exists")));

    const debugger_action_result_t disabled =
        service.set_managed_breakpoint_enabled(breakpoint, false);
    QVERIFY(disabled.success);
    QCOMPARE(service.managed_breakpoints().first().enabled, false);

    const debugger_action_result_t removed = service.remove_managed_breakpoint(breakpoint);
    QVERIFY(removed.success);
    QVERIFY(service.managed_breakpoints().isEmpty());

    const debugger_action_result_t missing_remove = service.remove_managed_breakpoint(breakpoint);
    QVERIFY(!missing_remove.success);
    QVERIFY(missing_remove.message.contains(QStringLiteral("not found")));

    debugger_managed_breakpoint_t function_breakpoint;
    function_breakpoint.function_name = QStringLiteral("main");
    QVERIFY(function_breakpoint.is_valid());
    QVERIFY(service.set_managed_breakpoint(function_breakpoint).success);
    QCOMPARE(service.managed_breakpoints().size(), 1);
    const debugger_action_result_t missing_toggle =
        service.set_managed_breakpoint_enabled(breakpoint, true);
    QVERIFY(!missing_toggle.success);
}

void tst_debugger_session_service_t::set_managed_breakpoints_filters_invalid_entries()
{
    debugger_session_service_t service;
    QSignalSpy managed_spy(&service, &i_debugger_session_service_t::managed_breakpoints_changed);
    QSignalSpy snapshot_spy(&service, &i_debugger_session_service_t::snapshot_changed);

    debugger_managed_breakpoint_t valid_file_breakpoint;
    valid_file_breakpoint.file_path = QStringLiteral("/tmp/project/a.cpp");
    valid_file_breakpoint.line = 3;
    debugger_managed_breakpoint_t invalid_breakpoint;
    debugger_managed_breakpoint_t valid_function_breakpoint;
    valid_function_breakpoint.function_name = QStringLiteral("worker");

    service.set_managed_breakpoints(
        {valid_file_breakpoint, invalid_breakpoint, valid_function_breakpoint});
    const QList<debugger_managed_breakpoint_t> stored = service.managed_breakpoints();
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.first().file_path, valid_file_breakpoint.file_path);
    QCOMPARE(stored.last().function_name, valid_function_breakpoint.function_name);
    QVERIFY(managed_spy.count() >= 1);
    QVERIFY(snapshot_spy.count() >= 1);
}

void tst_debugger_session_service_t::session_snapshot_tracks_hooked_runtime_states()
{
    QAction continue_action;
    QAction interrupt_action;
    QAction stop_action;
    continue_action.setEnabled(false);
    interrupt_action.setEnabled(false);
    stop_action.setEnabled(false);

    QStandardItemModel model;
    append_breakpoint_row(model, QString(), QStringLiteral("main"),
                          QStringLiteral("/tmp/project/main.cpp"), 17, QStringLiteral("i > 0"),
                          Qt::Checked);

    debugger_runtime_hooks_t hooks;
    hooks.action_for_command = [&](const char *command_id) -> QAction * {
        if (qstrcmp(command_id, Debugger::Constants::CONTINUE) == 0)
        {
            return &continue_action;
        }
        if (qstrcmp(command_id, Debugger::Constants::INTERRUPT) == 0)
        {
            return &interrupt_action;
        }
        if (qstrcmp(command_id, Debugger::Constants::STOP) == 0)
        {
            return &stop_action;
        }
        return nullptr;
    };
    hooks.breakpoint_model = [&model]() -> QAbstractItemModel * { return &model; };
    runtime_hooks_guard_t guard(hooks);

    debugger_session_service_t service;

    debugger_session_snapshot_t snapshot = service.session_snapshot();
    QVERIFY(!snapshot.active);
    QVERIFY(!snapshot.paused);
    QCOMPARE(snapshot.state_name, QStringLiteral("No active debugger session"));
    QCOMPARE(snapshot.breakpoint_count, 1);
    QCOMPARE(snapshot.engine_display_name, QStringLiteral("Qt Creator debugger"));
    QCOMPARE(snapshot.engine_type, QStringLiteral("qtcreator-actions"));

    continue_action.setEnabled(true);
    QVERIFY(service.has_active_session());
    QVERIFY(service.is_paused());
    snapshot = service.session_snapshot();
    QVERIFY(snapshot.active);
    QVERIFY(snapshot.paused);
    QCOMPARE(snapshot.state_name, QStringLiteral("Paused"));

    continue_action.setEnabled(false);
    interrupt_action.setEnabled(true);
    QVERIFY(service.has_active_session());
    QVERIFY(!service.is_paused());
    snapshot = service.session_snapshot();
    QCOMPARE(snapshot.state_name, QStringLiteral("Running"));

    interrupt_action.setEnabled(false);
    stop_action.setEnabled(true);
    snapshot = service.session_snapshot();
    QCOMPARE(snapshot.state_name, QStringLiteral("Active debugger session"));
}

void tst_debugger_session_service_t::breakpoint_snapshot_reads_model_rows_and_limits()
{
    QStandardItemModel model;
    append_breakpoint_row(model, QString(), QString(), QStringLiteral("/tmp/project/a.cpp"),
                          QStringLiteral("12"), QStringLiteral("ready"), std::nullopt);
    append_breakpoint_row(model, QStringLiteral("bp#2"), QStringLiteral("worker"),
                          QStringLiteral("/tmp/project/b.cpp"), QStringLiteral("oops"),
                          QStringLiteral("j == 0"), Qt::Unchecked);

    debugger_runtime_hooks_t hooks;
    hooks.breakpoint_model = [&model]() -> QAbstractItemModel * { return &model; };
    runtime_hooks_guard_t guard(hooks);

    debugger_session_service_t service;

    QVERIFY(service.breakpoints(-5).isEmpty());

    const QList<debugger_breakpoint_t> limited = service.breakpoints(1);
    QCOMPARE(limited.size(), 1);
    QCOMPARE(limited.first().model_id, 0);
    QCOMPARE(limited.first().file_path, QStringLiteral("/tmp/project/a.cpp"));
    QCOMPARE(limited.first().line, 12);
    QCOMPARE(limited.first().display_name, QStringLiteral("/tmp/project/a.cpp:12"));
    QCOMPARE(limited.first().enabled, true);

    const QList<debugger_breakpoint_t> all = service.breakpoints(10);
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.last().model_id, 1);
    QCOMPARE(all.last().display_name, QStringLiteral("bp#2"));
    QCOMPARE(all.last().function_name, QStringLiteral("worker"));
    QCOMPARE(all.last().line, -1);
    QCOMPARE(all.last().enabled, false);
    QCOMPARE(all.last().pending, false);
}

void tst_debugger_session_service_t::control_actions_cover_success_disabled_and_unavailable()
{
    QAction start_action;
    QAction continue_action;
    QAction next_action;
    QAction step_out_action;
    QAction stop_action;
    start_action.setEnabled(true);
    continue_action.setEnabled(true);
    next_action.setEnabled(false);
    step_out_action.setEnabled(true);
    stop_action.setEnabled(false);

    QSignalSpy start_spy(&start_action, &QAction::triggered);
    QSignalSpy continue_spy(&continue_action, &QAction::triggered);
    QSignalSpy step_out_spy(&step_out_action, &QAction::triggered);

    debugger_runtime_hooks_t hooks;
    hooks.action_for_command = [&](const char *command_id) -> QAction * {
        if (qstrcmp(command_id, Debugger::Constants::DEBUGGER_START) == 0)
        {
            return &start_action;
        }
        if (qstrcmp(command_id, Debugger::Constants::CONTINUE) == 0)
        {
            return &continue_action;
        }
        if (qstrcmp(command_id, Debugger::Constants::NEXT) == 0)
        {
            return &next_action;
        }
        if (qstrcmp(command_id, Debugger::Constants::STEPOUT) == 0)
        {
            return &step_out_action;
        }
        if (qstrcmp(command_id, Debugger::Constants::STOP) == 0)
        {
            return &stop_action;
        }
        return nullptr;
    };
    runtime_hooks_guard_t guard(hooks);

    debugger_session_service_t service;

    const debugger_action_result_t started = service.start_debugger();
    QVERIFY(started.success);
    QVERIFY(started.message.contains(QStringLiteral("Debugger start dispatched")));
    QCOMPARE(start_spy.count(), 1);

    const debugger_action_result_t continued = service.continue_execution();
    QVERIFY(continued.success);
    QVERIFY(continued.message.contains(QStringLiteral("Continue dispatched")));
    QCOMPARE(continue_spy.count(), 1);

    const debugger_action_result_t stepped_over = service.step_over();
    QVERIFY(!stepped_over.success);
    QVERIFY(stepped_over.message.contains(QStringLiteral("disabled")));

    const debugger_action_result_t stepped_into = service.step_into();
    QVERIFY(!stepped_into.success);
    QVERIFY(stepped_into.message.contains(QStringLiteral("unavailable")));

    const debugger_action_result_t stepped_out = service.step_out();
    QVERIFY(stepped_out.success);
    QVERIFY(stepped_out.message.contains(QStringLiteral("Step out dispatched")));
    QCOMPARE(step_out_spy.count(), 1);

    const debugger_action_result_t stopped = service.stop_debugger();
    QVERIFY(!stopped.success);
    QVERIFY(stopped.message.contains(QStringLiteral("disabled")));
}

void tst_debugger_session_service_t::runtime_mutation_hooks_cover_sync_and_remove_paths()
{
    QStandardItemModel model;
    debugger_managed_breakpoint_t added_breakpoint;
    added_breakpoint.file_path = QStringLiteral("/tmp/project/added.cpp");
    added_breakpoint.line = 10;
    added_breakpoint.enabled = true;

    debugger_managed_breakpoint_t existing_breakpoint;
    existing_breakpoint.file_path = QStringLiteral("/tmp/project/existing.cpp");
    existing_breakpoint.line = 20;
    existing_breakpoint.enabled = true;

    debugger_managed_breakpoint_t function_breakpoint;
    function_breakpoint.function_name = QStringLiteral("main");

    append_breakpoint_row(model, QStringLiteral("bp-existing"), QString(),
                          existing_breakpoint.file_path, existing_breakpoint.line, QString(),
                          Qt::Unchecked);

    bool mutation_available = true;
    QStringList runtime_existing;
    runtime_existing.append(breakpoint_key(existing_breakpoint));
    QStringList toggle_breakpoint_calls;
    QStringList toggle_enable_calls;

    debugger_runtime_hooks_t hooks;
    hooks.breakpoint_model = [&model]() -> QAbstractItemModel * { return &model; };
    hooks.mutation_available = [&mutation_available]() { return mutation_available; };
    hooks.managed_breakpoint_exists =
        [&runtime_existing](const debugger_managed_breakpoint_t &breakpoint) {
            return runtime_existing.contains(breakpoint_key(breakpoint));
        };
    hooks.toggle_managed_breakpoint = [&](const debugger_managed_breakpoint_t &breakpoint) {
        toggle_breakpoint_calls.append(breakpoint_key(breakpoint));
        const QString key = breakpoint_key(breakpoint);
        if (runtime_existing.contains(key))
        {
            runtime_existing.removeAll(key);
            const int row = find_breakpoint_row(model, breakpoint.file_path, breakpoint.line);
            if (row >= 0)
            {
                model.removeRow(row);
            }
            return;
        }

        runtime_existing.append(key);
        append_breakpoint_row(model, QString(), QString(), breakpoint.file_path, breakpoint.line,
                              breakpoint.condition, Qt::Unchecked);
    };
    hooks.toggle_managed_breakpoint_enabled =
        [&](const debugger_managed_breakpoint_t &breakpoint) {
            toggle_enable_calls.append(breakpoint_key(breakpoint));
            const int row = find_breakpoint_row(model, breakpoint.file_path, breakpoint.line);
            QVERIFY(row >= 0);
            model.setData(model.index(row, Debugger::Internal::BreakpointNumberColumn),
                          static_cast<int>(breakpoint.enabled ? Qt::Checked : Qt::Unchecked),
                          Qt::CheckStateRole);
        };
    runtime_hooks_guard_t guard(hooks);

    debugger_session_service_t service;

    service.set_managed_breakpoints({added_breakpoint, existing_breakpoint, function_breakpoint});
    QCOMPARE(toggle_breakpoint_calls, QStringList{breakpoint_key(added_breakpoint)});
    QVERIFY(toggle_enable_calls.contains(breakpoint_key(added_breakpoint)));
    QVERIFY(toggle_enable_calls.contains(breakpoint_key(existing_breakpoint)));
    QVERIFY(!toggle_enable_calls.contains(breakpoint_key(function_breakpoint)));

    debugger_managed_breakpoint_t removable_breakpoint;
    removable_breakpoint.file_path = QStringLiteral("/tmp/project/remove.cpp");
    removable_breakpoint.line = 30;

    mutation_available = false;
    service.set_managed_breakpoints({removable_breakpoint});
    const qsizetype calls_before_remove = toggle_breakpoint_calls.size();
    mutation_available = true;
    runtime_existing.append(breakpoint_key(removable_breakpoint));

    const debugger_action_result_t removed =
        service.remove_managed_breakpoint(removable_breakpoint);
    QVERIFY(removed.success);
    QCOMPARE(toggle_breakpoint_calls.size(), calls_before_remove + 1);

    mutation_available = false;
    service.set_managed_breakpoints({removable_breakpoint});
    const qsizetype calls_before_missing_runtime = toggle_breakpoint_calls.size();
    mutation_available = true;
    runtime_existing.removeAll(breakpoint_key(removable_breakpoint));

    const debugger_action_result_t removed_without_runtime =
        service.remove_managed_breakpoint(removable_breakpoint);
    QVERIFY(removed_without_runtime.success);
    QCOMPARE(toggle_breakpoint_calls.size(), calls_before_missing_runtime);

    mutation_available = false;
    service.set_managed_breakpoints({function_breakpoint});
    const qsizetype calls_before_function_remove = toggle_breakpoint_calls.size();
    mutation_available = true;

    const debugger_action_result_t removed_function =
        service.remove_managed_breakpoint(function_breakpoint);
    QVERIFY(removed_function.success);
    QCOMPARE(toggle_breakpoint_calls.size(), calls_before_function_remove);
}

QTEST_MAIN(tst_debugger_session_service_t)

#include "tst_debugger_session_service.moc"
