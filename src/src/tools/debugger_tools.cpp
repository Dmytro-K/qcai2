/*! @file
    @brief Implements debugger tools for session inspection, control, and breakpoints.
*/

#include "debugger_tools.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace qcai2
{

namespace
{

QString no_debugger_service_message()
{
    return QStringLiteral("Debugger integration is not available.");
}

QString no_active_debugger_message()
{
    return QStringLiteral("No active debugger session.");
}

QJsonObject max_items_schema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("description"), QStringLiteral("Maximum number of items to return")}};
}

QString format_json_array_result(const QString &title, const QJsonArray &items)
{
    if (items.isEmpty())
    {
        return title + QStringLiteral("\n[]");
    }

    return title + QLatin1Char('\n') +
           QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Indented));
}

debugger_managed_breakpoint_t breakpoint_from_args(const QJsonObject &args)
{
    debugger_managed_breakpoint_t breakpoint;
    breakpoint.file_path = args.value(QStringLiteral("file_path")).toString();
    breakpoint.line = args.value(QStringLiteral("line")).toInt(-1);
    breakpoint.function_name = args.value(QStringLiteral("function_name")).toString();
    breakpoint.condition = args.value(QStringLiteral("condition")).toString();
    breakpoint.enabled = args.value(QStringLiteral("enabled")).toBool(true);
    return breakpoint;
}

QJsonObject breakpoint_schema()
{
    return QJsonObject{
        {QStringLiteral("file_path"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Source file path")}}},
        {QStringLiteral("line"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("description"), QStringLiteral("One-based source line")}}},
        {QStringLiteral("function_name"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("string")},
             {QStringLiteral("description"), QStringLiteral("Function breakpoint target")}}},
        {QStringLiteral("condition"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("string")},
             {QStringLiteral("description"), QStringLiteral("Optional breakpoint condition")}}},
        {QStringLiteral("enabled"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                     {QStringLiteral("description"),
                      QStringLiteral("Whether the breakpoint starts enabled")}}}};
}

}  // namespace

debugger_tool_base_t::debugger_tool_base_t(i_debugger_session_service_t *debugger_service)
    : debugger_service(debugger_service)
{
}

QString get_debug_session_state_tool_t::name() const
{
    return QStringLiteral("get_debug_session_state");
}
QString get_debug_session_state_tool_t::description() const
{
    return QStringLiteral(
        "Show the current Qt Creator debugger session state, location, and counts.");
}
QJsonObject get_debug_session_state_tool_t::args_schema() const
{
    return QJsonObject{};
}
QString get_debug_session_state_tool_t::execute(const QJsonObject &, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }
    return format_debugger_snapshot_json(this->debugger_service->session_snapshot());
}

QString get_stack_frames_tool_t::name() const
{
    return QStringLiteral("get_stack_frames");
}
QString get_stack_frames_tool_t::description() const
{
    return QStringLiteral("List stack frames from the active debugger session.");
}
QJsonObject get_stack_frames_tool_t::args_schema() const
{
    return QJsonObject{{QStringLiteral("max_frames"), max_items_schema()}};
}
QString get_stack_frames_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }
    if (!this->debugger_service->has_active_session())
    {
        return no_active_debugger_message();
    }

    const int max_frames = qBound(1, args.value(QStringLiteral("max_frames")).toInt(50), 500);
    QJsonArray items;
    for (const debugger_stack_frame_t &frame : this->debugger_service->stack_frames(max_frames))
    {
        items.append(frame.to_json());
    }
    return format_json_array_result(QStringLiteral("Stack frames:"), items);
}

QString get_threads_tool_t::name() const
{
    return QStringLiteral("get_threads");
}
QString get_threads_tool_t::description() const
{
    return QStringLiteral("List threads from the active debugger session.");
}
QJsonObject get_threads_tool_t::args_schema() const
{
    return QJsonObject{{QStringLiteral("max_threads"), max_items_schema()}};
}
QString get_threads_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }
    if (!this->debugger_service->has_active_session())
    {
        return no_active_debugger_message();
    }

    const int max_threads = qBound(1, args.value(QStringLiteral("max_threads")).toInt(100), 1000);
    QJsonArray items;
    for (const debugger_thread_t &thread : this->debugger_service->threads(max_threads))
    {
        items.append(thread.to_json());
    }
    return format_json_array_result(QStringLiteral("Threads:"), items);
}

QString get_locals_tool_t::name() const
{
    return QStringLiteral("get_locals");
}
QString get_locals_tool_t::description() const
{
    return QStringLiteral("Show local variables from the active debugger session.");
}
QJsonObject get_locals_tool_t::args_schema() const
{
    return QJsonObject{{QStringLiteral("max_items"), max_items_schema()},
                       {QStringLiteral("max_depth"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                    {QStringLiteral("description"),
                                     QStringLiteral("Maximum variable tree depth to include")}}}};
}
QString get_locals_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }
    if (!this->debugger_service->has_active_session())
    {
        return no_active_debugger_message();
    }

    const int max_items = qBound(1, args.value(QStringLiteral("max_items")).toInt(100), 1000);
    const int max_depth = qBound(0, args.value(QStringLiteral("max_depth")).toInt(1), 10);
    QJsonArray items;
    for (const debugger_variable_t &variable :
         this->debugger_service->locals(max_items, max_depth))
    {
        items.append(variable.to_json());
    }
    return format_json_array_result(QStringLiteral("Locals:"), items);
}

QString get_watch_expressions_tool_t::name() const
{
    return QStringLiteral("get_watch_expressions");
}
QString get_watch_expressions_tool_t::description() const
{
    return QStringLiteral("Show watch expressions from the active debugger session.");
}
QJsonObject get_watch_expressions_tool_t::args_schema() const
{
    return QJsonObject{
        {QStringLiteral("max_items"), max_items_schema()},
        {QStringLiteral("max_depth"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("description"),
                      QStringLiteral("Maximum watch-expression tree depth to include")}}}};
}
QString get_watch_expressions_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }
    if (!this->debugger_service->has_active_session())
    {
        return no_active_debugger_message();
    }

    const int max_items = qBound(1, args.value(QStringLiteral("max_items")).toInt(100), 1000);
    const int max_depth = qBound(0, args.value(QStringLiteral("max_depth")).toInt(1), 10);
    QJsonArray items;
    for (const debugger_variable_t &variable :
         this->debugger_service->watch_expressions(max_items, max_depth))
    {
        items.append(variable.to_json());
    }
    return format_json_array_result(QStringLiteral("Watch expressions:"), items);
}

QString get_breakpoints_tool_t::name() const
{
    return QStringLiteral("get_breakpoints");
}
QString get_breakpoints_tool_t::description() const
{
    return QStringLiteral("List current Qt Creator debugger breakpoints.");
}
QJsonObject get_breakpoints_tool_t::args_schema() const
{
    return QJsonObject{{QStringLiteral("max_items"), max_items_schema()}};
}
QString get_breakpoints_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }

    const int max_items = qBound(1, args.value(QStringLiteral("max_items")).toInt(200), 2000);
    QJsonArray items;
    for (const debugger_breakpoint_t &breakpoint : this->debugger_service->breakpoints(max_items))
    {
        items.append(breakpoint.to_json());
    }
    return format_json_array_result(QStringLiteral("Breakpoints:"), items);
}

QString show_debugger_console_tool_t::name() const
{
    return QStringLiteral("show_debugger_console");
}
QString show_debugger_console_tool_t::description() const
{
    return QStringLiteral("Show recent debugger console/log output and diagnostics.");
}
QJsonObject show_debugger_console_tool_t::args_schema() const
{
    return QJsonObject{
        {QStringLiteral("max_lines"), max_items_schema()},
        {QStringLiteral("diagnostics_only"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                     {QStringLiteral("description"),
                      QStringLiteral("Return only extracted debugger diagnostics")}}}};
}
QString show_debugger_console_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }

    const int max_lines = qBound(10, args.value(QStringLiteral("max_lines")).toInt(200), 4000);
    const bool diagnostics_only = args.value(QStringLiteral("diagnostics_only")).toBool(false);
    const QString diagnostics = this->debugger_service->debugger_diagnostics_snapshot(50);
    if (diagnostics_only)
    {
        return diagnostics;
    }

    return QStringLiteral("%1\n\nRecent Debugger Output:\n%2")
        .arg(diagnostics, this->debugger_service->debugger_output_snapshot(max_lines));
}

#define QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(CLASS_NAME, TOOL_NAME, TOOL_DESCRIPTION, CALL_EXPR)    \
    QString CLASS_NAME::name() const                                                              \
    {                                                                                             \
        return QStringLiteral(TOOL_NAME);                                                         \
    }                                                                                             \
    QString CLASS_NAME::description() const                                                       \
    {                                                                                             \
        return QStringLiteral(TOOL_DESCRIPTION);                                                  \
    }                                                                                             \
    QJsonObject CLASS_NAME::args_schema() const                                                   \
    {                                                                                             \
        return QJsonObject{};                                                                     \
    }                                                                                             \
    QString CLASS_NAME::execute(const QJsonObject &, const QString &)                             \
    {                                                                                             \
        if (this->debugger_service == nullptr)                                                    \
        {                                                                                         \
            return no_debugger_service_message();                                                 \
        }                                                                                         \
        return format_debugger_action_result_json(this->debugger_service->CALL_EXPR());           \
    }                                                                                             \
    bool CLASS_NAME::requires_approval() const                                                    \
    {                                                                                             \
        return true;                                                                              \
    }

QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(debugger_continue_tool_t, "debugger_continue",
                                   "Continue the active debugger session. Requires approval.",
                                   continue_execution)
QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(
    debugger_start_tool_t, "debugger_start",
    "Start debugging the current Qt Creator run configuration. Requires approval.", start_debugger)
QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(debugger_step_over_tool_t, "debugger_step_over",
                                   "Step over in the active debugger session. Requires approval.",
                                   step_over)
QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(debugger_step_into_tool_t, "debugger_step_into",
                                   "Step into in the active debugger session. Requires approval.",
                                   step_into)
QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(debugger_step_out_tool_t, "debugger_step_out",
                                   "Step out in the active debugger session. Requires approval.",
                                   step_out)
QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL(debugger_stop_tool_t, "debugger_stop",
                                   "Stop the active debugger session. Requires approval.",
                                   stop_debugger)

#undef QCAI2_DEFINE_DEBUGGER_CONTROL_TOOL

QString set_breakpoint_tool_t::name() const
{
    return QStringLiteral("set_breakpoint");
}
QString set_breakpoint_tool_t::description() const
{
    return QStringLiteral("Create a persistent managed breakpoint by file+line or function name. "
                          "Requires approval.");
}
QJsonObject set_breakpoint_tool_t::args_schema() const
{
    return breakpoint_schema();
}
QString set_breakpoint_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }

    return format_debugger_action_result_json(
        this->debugger_service->set_managed_breakpoint(breakpoint_from_args(args)));
}
bool set_breakpoint_tool_t::requires_approval() const
{
    return true;
}

QString remove_breakpoint_tool_t::name() const
{
    return QStringLiteral("remove_breakpoint");
}
QString remove_breakpoint_tool_t::description() const
{
    return QStringLiteral("Remove a persistent managed breakpoint by file+line or function name. "
                          "Requires approval.");
}
QJsonObject remove_breakpoint_tool_t::args_schema() const
{
    return breakpoint_schema();
}
QString remove_breakpoint_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }

    return format_debugger_action_result_json(
        this->debugger_service->remove_managed_breakpoint(breakpoint_from_args(args)));
}
bool remove_breakpoint_tool_t::requires_approval() const
{
    return true;
}

QString enable_breakpoint_tool_t::name() const
{
    return QStringLiteral("enable_breakpoint");
}
QString enable_breakpoint_tool_t::description() const
{
    return QStringLiteral("Enable a persistent managed breakpoint. Requires approval.");
}
QJsonObject enable_breakpoint_tool_t::args_schema() const
{
    return breakpoint_schema();
}
QString enable_breakpoint_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }

    return format_debugger_action_result_json(
        this->debugger_service->set_managed_breakpoint_enabled(breakpoint_from_args(args), true));
}
bool enable_breakpoint_tool_t::requires_approval() const
{
    return true;
}

QString disable_breakpoint_tool_t::name() const
{
    return QStringLiteral("disable_breakpoint");
}
QString disable_breakpoint_tool_t::description() const
{
    return QStringLiteral("Disable a persistent managed breakpoint. Requires approval.");
}
QJsonObject disable_breakpoint_tool_t::args_schema() const
{
    return breakpoint_schema();
}
QString disable_breakpoint_tool_t::execute(const QJsonObject &args, const QString &)
{
    if (this->debugger_service == nullptr)
    {
        return no_debugger_service_message();
    }

    return format_debugger_action_result_json(
        this->debugger_service->set_managed_breakpoint_enabled(breakpoint_from_args(args), false));
}
bool disable_breakpoint_tool_t::requires_approval() const
{
    return true;
}

void register_debugger_tools(tool_registry_t *registry,
                             i_debugger_session_service_t *debugger_service)
{
    if (registry == nullptr)
    {
        return;
    }

    registry->register_tool(std::make_shared<get_debug_session_state_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<get_stack_frames_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<get_threads_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<get_locals_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<get_watch_expressions_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<get_breakpoints_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<show_debugger_console_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<debugger_start_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<debugger_continue_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<debugger_step_over_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<debugger_step_into_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<debugger_step_out_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<debugger_stop_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<set_breakpoint_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<remove_breakpoint_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<enable_breakpoint_tool_t>(debugger_service));
    registry->register_tool(std::make_shared<disable_breakpoint_tool_t>(debugger_service));
}

}  // namespace qcai2
