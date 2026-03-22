#include "../src/debugger/debugger_session_service.h"
#include "../src/tools/debugger_tools.h"

#include <QtTest>

using namespace qcai2;

namespace
{

class fake_debugger_service_t final : public i_debugger_session_service_t
{
    Q_OBJECT

public:
    explicit fake_debugger_service_t(QObject *parent = nullptr)
        : i_debugger_session_service_t(parent)
    {
    }

    bool active = true;
    debugger_session_snapshot_t snapshot;
    QList<debugger_stack_frame_t> stack;
    QList<debugger_thread_t> thread_list;
    QList<debugger_variable_t> local_list;
    QList<debugger_variable_t> watch_list;
    QList<debugger_breakpoint_t> breakpoint_list;
    QList<debugger_managed_breakpoint_t> managed;
    mutable int last_stack_limit = -1;
    mutable int last_threads_limit = -1;
    mutable int last_locals_max_items = -1;
    mutable int last_locals_max_depth = -1;
    mutable int last_watch_max_items = -1;
    mutable int last_watch_max_depth = -1;
    mutable int last_breakpoints_limit = -1;
    mutable int last_output_max_lines = -1;
    int set_calls = 0;
    int remove_calls = 0;
    int enable_disable_calls = 0;
    bool last_enabled_value = false;
    debugger_managed_breakpoint_t last_set_breakpoint;
    debugger_managed_breakpoint_t last_removed_breakpoint;
    debugger_managed_breakpoint_t last_toggled_breakpoint;

    bool has_active_session() const override
    {
        return this->active;
    }
    bool is_paused() const override
    {
        return snapshot.paused;
    }
    debugger_session_snapshot_t session_snapshot() const override
    {
        return snapshot;
    }
    QList<debugger_stack_frame_t> stack_frames(int max_items) const override
    {
        this->last_stack_limit = max_items;
        return stack;
    }
    QList<debugger_thread_t> threads(int max_items) const override
    {
        this->last_threads_limit = max_items;
        return thread_list;
    }
    QList<debugger_variable_t> locals(int max_items, int max_depth) const override
    {
        this->last_locals_max_items = max_items;
        this->last_locals_max_depth = max_depth;
        return local_list;
    }
    QList<debugger_variable_t> watch_expressions(int max_items, int max_depth) const override
    {
        this->last_watch_max_items = max_items;
        this->last_watch_max_depth = max_depth;
        return watch_list;
    }
    QList<debugger_breakpoint_t> breakpoints(int max_items) const override
    {
        this->last_breakpoints_limit = max_items;
        return breakpoint_list;
    }
    QString debugger_output_snapshot(int max_lines) const override
    {
        this->last_output_max_lines = max_lines;
        return QStringLiteral("output:%1").arg(max_lines);
    }
    QString debugger_diagnostics_snapshot(int) const override
    {
        return QStringLiteral("[error] fake.cpp:12 — boom");
    }
    debugger_action_result_t start_debugger() override
    {
        return {true, QStringLiteral("start")};
    }
    debugger_action_result_t continue_execution() override
    {
        return {true, QStringLiteral("continue")};
    }
    debugger_action_result_t step_over() override
    {
        return {true, QStringLiteral("step over")};
    }
    debugger_action_result_t step_into() override
    {
        return {true, QStringLiteral("step into")};
    }
    debugger_action_result_t step_out() override
    {
        return {true, QStringLiteral("step out")};
    }
    debugger_action_result_t stop_debugger() override
    {
        return {true, QStringLiteral("stop")};
    }
    debugger_action_result_t
    set_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint) override
    {
        ++this->set_calls;
        this->last_set_breakpoint = breakpoint;
        this->managed.append(breakpoint);
        emit this->managed_breakpoints_changed();
        return {true, QStringLiteral("added")};
    }
    debugger_action_result_t
    remove_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint) override
    {
        ++this->remove_calls;
        this->last_removed_breakpoint = breakpoint;
        this->managed.clear();
        emit this->managed_breakpoints_changed();
        return {true, QStringLiteral("removed")};
    }
    debugger_action_result_t
    set_managed_breakpoint_enabled(const debugger_managed_breakpoint_t &breakpoint,
                                   bool enabled) override
    {
        ++this->enable_disable_calls;
        this->last_toggled_breakpoint = breakpoint;
        this->last_enabled_value = enabled;
        return {true, enabled ? QStringLiteral("enabled") : QStringLiteral("disabled")};
    }
    QList<debugger_managed_breakpoint_t> managed_breakpoints() const override
    {
        return managed;
    }
    void set_managed_breakpoints(const QList<debugger_managed_breakpoint_t> &breakpoints) override
    {
        this->managed = breakpoints;
        emit this->managed_breakpoints_changed();
    }
};

}  // namespace

class tst_debugger_tools_t : public QObject
{
    Q_OBJECT

private slots:
    void get_state_formats_snapshot();
    void no_active_session_returns_clear_message();
    void readonly_tools_clamp_limits_and_render_json();
    void debugger_console_tool_supports_modes();
    void tool_metadata_and_schemas_are_stable();
    void control_tools_require_approval();
    void mutation_tools_cover_add_enable_disable_remove();
    void readonly_tools_render_empty_arrays();
    void null_service_returns_clear_messages();
    void register_debugger_tools_registers_expected_catalog();
};

void tst_debugger_tools_t::get_state_formats_snapshot()
{
    fake_debugger_service_t service;
    service.snapshot.active = true;
    service.snapshot.paused = true;
    service.snapshot.state_name = QStringLiteral("InferiorStopOk");
    service.snapshot.engine_display_name = QStringLiteral("Test Debugger");

    get_debug_session_state_tool_t tool(&service);
    const QString result = tool.execute(QJsonObject{}, QString());
    QVERIFY(result.contains(QStringLiteral("Test Debugger")));
    QVERIFY(result.contains(QStringLiteral("InferiorStopOk")));
}

void tst_debugger_tools_t::no_active_session_returns_clear_message()
{
    fake_debugger_service_t service;
    service.active = false;

    get_stack_frames_tool_t tool(&service);
    QCOMPARE(tool.execute(QJsonObject{}, QString()),
             QStringLiteral("No active debugger session."));
    get_threads_tool_t threads_tool(&service);
    QCOMPARE(threads_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("No active debugger session."));
    get_locals_tool_t locals_tool(&service);
    QCOMPARE(locals_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("No active debugger session."));
    get_watch_expressions_tool_t watch_tool(&service);
    QCOMPARE(watch_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("No active debugger session."));
}

void tst_debugger_tools_t::readonly_tools_clamp_limits_and_render_json()
{
    fake_debugger_service_t service;

    debugger_stack_frame_t frame;
    frame.function_name = QStringLiteral("main");
    frame.file_path = QStringLiteral("/tmp/main.cpp");
    frame.line = 7;
    service.stack.append(frame);

    debugger_thread_t thread;
    thread.id = QStringLiteral("1");
    thread.name = QStringLiteral("worker");
    service.thread_list.append(thread);

    debugger_variable_t local;
    local.name = QStringLiteral("counter");
    local.value = QStringLiteral("42");
    service.local_list.append(local);

    debugger_variable_t watch;
    watch.name = QStringLiteral("watcher");
    service.watch_list.append(watch);

    debugger_breakpoint_t breakpoint;
    breakpoint.file_path = QStringLiteral("/tmp/main.cpp");
    breakpoint.line = 9;
    service.breakpoint_list.append(breakpoint);

    get_stack_frames_tool_t stack_tool(&service);
    const QString stack_result =
        stack_tool.execute(QJsonObject{{QStringLiteral("max_frames"), 9999}}, QString());
    QCOMPARE(service.last_stack_limit, 500);
    QVERIFY(stack_result.contains(QStringLiteral("Stack frames:")));
    QVERIFY(stack_result.contains(QStringLiteral("/tmp/main.cpp")));

    get_threads_tool_t threads_tool(&service);
    const QString threads_result =
        threads_tool.execute(QJsonObject{{QStringLiteral("max_threads"), 0}}, QString());
    QCOMPARE(service.last_threads_limit, 1);
    QVERIFY(threads_result.contains(QStringLiteral("Threads:")));
    QVERIFY(threads_result.contains(QStringLiteral("worker")));

    get_locals_tool_t locals_tool(&service);
    const QString locals_result = locals_tool.execute(
        QJsonObject{{QStringLiteral("max_items"), -5}, {QStringLiteral("max_depth"), 99}},
        QString());
    QCOMPARE(service.last_locals_max_items, 1);
    QCOMPARE(service.last_locals_max_depth, 10);
    QVERIFY(locals_result.contains(QStringLiteral("Locals:")));
    QVERIFY(locals_result.contains(QStringLiteral("counter")));

    get_watch_expressions_tool_t watch_tool(&service);
    const QString watch_result = watch_tool.execute(
        QJsonObject{{QStringLiteral("max_items"), 1001}, {QStringLiteral("max_depth"), -1}},
        QString());
    QCOMPARE(service.last_watch_max_items, 1000);
    QCOMPARE(service.last_watch_max_depth, 0);
    QVERIFY(watch_result.contains(QStringLiteral("Watch expressions:")));
    QVERIFY(watch_result.contains(QStringLiteral("watcher")));

    get_breakpoints_tool_t breakpoints_tool(&service);
    const QString breakpoints_result =
        breakpoints_tool.execute(QJsonObject{{QStringLiteral("max_items"), 99999}}, QString());
    QCOMPARE(service.last_breakpoints_limit, 2000);
    QVERIFY(breakpoints_result.contains(QStringLiteral("Breakpoints:")));
    QVERIFY(breakpoints_result.contains(QStringLiteral("/tmp/main.cpp")));
}

void tst_debugger_tools_t::debugger_console_tool_supports_modes()
{
    fake_debugger_service_t service;
    show_debugger_console_tool_t tool(&service);

    const QString diagnostics_only =
        tool.execute(QJsonObject{{QStringLiteral("diagnostics_only"), true}}, QString());
    QCOMPARE(diagnostics_only, QStringLiteral("[error] fake.cpp:12 — boom"));
    QCOMPARE(service.last_output_max_lines, -1);

    const QString full_output =
        tool.execute(QJsonObject{{QStringLiteral("max_lines"), 99999}}, QString());
    QCOMPARE(service.last_output_max_lines, 4000);
    QVERIFY(full_output.contains(QStringLiteral("Recent Debugger Output:")));
    QVERIFY(full_output.contains(QStringLiteral("output:4000")));
    QVERIFY(full_output.contains(QStringLiteral("[error] fake.cpp:12")));

    const QString clamped_low_output =
        tool.execute(QJsonObject{{QStringLiteral("max_lines"), 0}}, QString());
    QCOMPARE(service.last_output_max_lines, 10);
    QVERIFY(clamped_low_output.contains(QStringLiteral("output:10")));
}

void tst_debugger_tools_t::tool_metadata_and_schemas_are_stable()
{
    fake_debugger_service_t service;

    get_debug_session_state_tool_t state_tool(&service);
    get_stack_frames_tool_t stack_tool(&service);
    get_threads_tool_t threads_tool(&service);
    get_locals_tool_t locals_tool(&service);
    get_watch_expressions_tool_t watch_tool(&service);
    get_breakpoints_tool_t breakpoints_tool(&service);
    show_debugger_console_tool_t console_tool(&service);
    debugger_start_tool_t start_tool(&service);
    debugger_continue_tool_t continue_tool(&service);
    debugger_step_over_tool_t step_over_tool(&service);
    debugger_step_into_tool_t step_into_tool(&service);
    debugger_step_out_tool_t step_out_tool(&service);
    debugger_stop_tool_t stop_tool(&service);
    set_breakpoint_tool_t set_tool(&service);
    remove_breakpoint_tool_t remove_tool(&service);
    enable_breakpoint_tool_t enable_tool(&service);
    disable_breakpoint_tool_t disable_tool(&service);

    QCOMPARE(state_tool.name(), QStringLiteral("get_debug_session_state"));
    QVERIFY(
        state_tool.description().contains(QStringLiteral("Qt Creator debugger session state")));
    QVERIFY(state_tool.args_schema().isEmpty());

    QCOMPARE(stack_tool.name(), QStringLiteral("get_stack_frames"));
    QVERIFY(stack_tool.description().contains(QStringLiteral("stack frames")));
    QVERIFY(stack_tool.args_schema().contains(QStringLiteral("max_frames")));

    QCOMPARE(threads_tool.name(), QStringLiteral("get_threads"));
    QVERIFY(threads_tool.description().contains(QStringLiteral("threads")));
    QVERIFY(threads_tool.args_schema().contains(QStringLiteral("max_threads")));

    QCOMPARE(locals_tool.name(), QStringLiteral("get_locals"));
    QVERIFY(locals_tool.description().contains(QStringLiteral("local variables")));
    QVERIFY(locals_tool.args_schema().contains(QStringLiteral("max_items")));
    QVERIFY(locals_tool.args_schema().contains(QStringLiteral("max_depth")));

    QCOMPARE(watch_tool.name(), QStringLiteral("get_watch_expressions"));
    QVERIFY(watch_tool.description().contains(QStringLiteral("watch expressions")));
    QVERIFY(watch_tool.args_schema().contains(QStringLiteral("max_depth")));

    QCOMPARE(breakpoints_tool.name(), QStringLiteral("get_breakpoints"));
    QVERIFY(breakpoints_tool.description().contains(QStringLiteral("breakpoints")));
    QVERIFY(breakpoints_tool.args_schema().contains(QStringLiteral("max_items")));

    QCOMPARE(console_tool.name(), QStringLiteral("show_debugger_console"));
    QVERIFY(console_tool.description().contains(QStringLiteral("debugger console")));
    QVERIFY(console_tool.args_schema().contains(QStringLiteral("diagnostics_only")));

    QCOMPARE(start_tool.name(), QStringLiteral("debugger_start"));
    QVERIFY(start_tool.description().contains(QStringLiteral("Start debugging")));
    QVERIFY(start_tool.args_schema().isEmpty());
    QCOMPARE(continue_tool.name(), QStringLiteral("debugger_continue"));
    QVERIFY(continue_tool.description().contains(QStringLiteral("Continue")));
    QVERIFY(continue_tool.args_schema().isEmpty());
    QCOMPARE(step_over_tool.name(), QStringLiteral("debugger_step_over"));
    QCOMPARE(step_into_tool.name(), QStringLiteral("debugger_step_into"));
    QCOMPARE(step_out_tool.name(), QStringLiteral("debugger_step_out"));
    QCOMPARE(stop_tool.name(), QStringLiteral("debugger_stop"));

    QCOMPARE(set_tool.name(), QStringLiteral("set_breakpoint"));
    QVERIFY(set_tool.description().contains(QStringLiteral("persistent managed breakpoint")));
    QVERIFY(set_tool.args_schema().contains(QStringLiteral("file_path")));
    QVERIFY(set_tool.args_schema().contains(QStringLiteral("function_name")));
    QVERIFY(set_tool.args_schema().contains(QStringLiteral("condition")));
    QVERIFY(set_tool.args_schema().contains(QStringLiteral("enabled")));

    QCOMPARE(remove_tool.name(), QStringLiteral("remove_breakpoint"));
    QCOMPARE(enable_tool.name(), QStringLiteral("enable_breakpoint"));
    QCOMPARE(disable_tool.name(), QStringLiteral("disable_breakpoint"));
}

void tst_debugger_tools_t::control_tools_require_approval()
{
    fake_debugger_service_t service;
    debugger_start_tool_t start_tool(&service);
    debugger_continue_tool_t continue_tool(&service);
    debugger_step_over_tool_t step_over_tool(&service);
    debugger_step_into_tool_t step_into_tool(&service);
    debugger_step_out_tool_t step_out_tool(&service);
    debugger_stop_tool_t stop_tool(&service);
    QVERIFY(start_tool.requires_approval());
    QVERIFY(continue_tool.requires_approval());
    QVERIFY(step_over_tool.requires_approval());
    QVERIFY(step_into_tool.requires_approval());
    QVERIFY(step_out_tool.requires_approval());
    QVERIFY(stop_tool.requires_approval());
    QVERIFY(start_tool.execute(QJsonObject{}, QString()).contains(QStringLiteral("start")));
    QVERIFY(continue_tool.execute(QJsonObject{}, QString()).contains(QStringLiteral("continue")));
    QVERIFY(
        step_over_tool.execute(QJsonObject{}, QString()).contains(QStringLiteral("step over")));
    QVERIFY(
        step_into_tool.execute(QJsonObject{}, QString()).contains(QStringLiteral("step into")));
    QVERIFY(step_out_tool.execute(QJsonObject{}, QString()).contains(QStringLiteral("step out")));
    QVERIFY(stop_tool.execute(QJsonObject{}, QString()).contains(QStringLiteral("stop")));
}

void tst_debugger_tools_t::mutation_tools_cover_add_enable_disable_remove()
{
    fake_debugger_service_t service;
    set_breakpoint_tool_t set_tool(&service);
    enable_breakpoint_tool_t enable_tool(&service);
    disable_breakpoint_tool_t disable_tool(&service);
    remove_breakpoint_tool_t remove_tool(&service);
    const QString result = set_tool.execute(
        QJsonObject{{QStringLiteral("file_path"), QStringLiteral("/tmp/project/main.cpp")},
                    {QStringLiteral("line"), 17}},
        QString());
    QVERIFY(result.contains(QStringLiteral("added")));
    QCOMPARE(service.set_calls, 1);
    QCOMPARE(service.managed.size(), 1);
    QCOMPARE(service.last_set_breakpoint.file_path, QStringLiteral("/tmp/project/main.cpp"));
    QCOMPARE(service.managed.first().line, 17);
    QVERIFY(enable_tool.requires_approval());
    QVERIFY(disable_tool.requires_approval());
    QVERIFY(remove_tool.requires_approval());

    const QString enable_result =
        enable_tool.execute(QJsonObject{{QStringLiteral("function_name"), QStringLiteral("main")},
                                        {QStringLiteral("condition"), QStringLiteral("argc > 1")}},
                            QString());
    QVERIFY(enable_result.contains(QStringLiteral("enabled")));
    QCOMPARE(service.enable_disable_calls, 1);
    QCOMPARE(service.last_enabled_value, true);
    QCOMPARE(service.last_toggled_breakpoint.function_name, QStringLiteral("main"));
    QCOMPARE(service.last_toggled_breakpoint.condition, QStringLiteral("argc > 1"));
    QCOMPARE(service.last_toggled_breakpoint.enabled, true);

    const QString disable_result = disable_tool.execute(
        QJsonObject{{QStringLiteral("file_path"), QStringLiteral("/tmp/project/main.cpp")},
                    {QStringLiteral("line"), 17},
                    {QStringLiteral("enabled"), false}},
        QString());
    QVERIFY(disable_result.contains(QStringLiteral("disabled")));
    QCOMPARE(service.enable_disable_calls, 2);
    QCOMPARE(service.last_enabled_value, false);
    QCOMPARE(service.last_toggled_breakpoint.file_path, QStringLiteral("/tmp/project/main.cpp"));
    QCOMPARE(service.last_toggled_breakpoint.line, 17);

    const QString remove_result = remove_tool.execute(
        QJsonObject{{QStringLiteral("function_name"), QStringLiteral("main")}}, QString());
    QVERIFY(remove_result.contains(QStringLiteral("removed")));
    QCOMPARE(service.remove_calls, 1);
    QCOMPARE(service.last_removed_breakpoint.function_name, QStringLiteral("main"));
    QVERIFY(service.managed.isEmpty());
}

void tst_debugger_tools_t::readonly_tools_render_empty_arrays()
{
    fake_debugger_service_t service;

    get_stack_frames_tool_t stack_tool(&service);
    get_threads_tool_t threads_tool(&service);
    get_locals_tool_t locals_tool(&service);
    get_watch_expressions_tool_t watch_tool(&service);
    get_breakpoints_tool_t breakpoints_tool(&service);

    QVERIFY(stack_tool.execute(QJsonObject{}, QString()).endsWith(QStringLiteral("\n[]")));
    QVERIFY(threads_tool.execute(QJsonObject{}, QString()).endsWith(QStringLiteral("\n[]")));
    QVERIFY(locals_tool.execute(QJsonObject{}, QString()).endsWith(QStringLiteral("\n[]")));
    QVERIFY(watch_tool.execute(QJsonObject{}, QString()).endsWith(QStringLiteral("\n[]")));
    QVERIFY(breakpoints_tool.execute(QJsonObject{}, QString()).endsWith(QStringLiteral("\n[]")));
}

void tst_debugger_tools_t::null_service_returns_clear_messages()
{
    get_debug_session_state_tool_t state_tool(nullptr);
    show_debugger_console_tool_t console_tool(nullptr);
    debugger_start_tool_t start_tool(nullptr);
    debugger_continue_tool_t continue_tool(nullptr);
    set_breakpoint_tool_t set_tool(nullptr);

    QCOMPARE(state_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("Debugger integration is not available."));
    QCOMPARE(console_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("Debugger integration is not available."));
    QCOMPARE(start_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("Debugger integration is not available."));
    QCOMPARE(continue_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("Debugger integration is not available."));
    QCOMPARE(set_tool.execute(QJsonObject{}, QString()),
             QStringLiteral("Debugger integration is not available."));
}

void tst_debugger_tools_t::register_debugger_tools_registers_expected_catalog()
{
    fake_debugger_service_t service;
    tool_registry_t registry;
    register_debugger_tools(nullptr, &service);
    register_debugger_tools(&registry, &service);

    QCOMPARE(registry.all_tools().size(), 17);
    QVERIFY(registry.tool(QStringLiteral("get_debug_session_state")) != nullptr);
    QVERIFY(registry.tool(QStringLiteral("show_debugger_console")) != nullptr);
    QVERIFY(registry.tool(QStringLiteral("debugger_start")) != nullptr);
    QVERIFY(registry.tool(QStringLiteral("debugger_step_into")) != nullptr);
    QVERIFY(registry.tool(QStringLiteral("disable_breakpoint")) != nullptr);
}

QTEST_GUILESS_MAIN(tst_debugger_tools_t)

#include "tst_debugger_tools.moc"
