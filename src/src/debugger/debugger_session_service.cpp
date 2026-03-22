/*! @file
    @brief Implements debugger session snapshots, output capture, and control dispatch.
*/

#include "debugger_session_service.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>

#include <debugger/breakhandler.h>
#include <debugger/debuggerconstants.h>
#include <debugger/debuggerinternalconstants.h>
#include <debugger/debuggerprotocol.h>

#include <utils/filepath.h>
#include <utils/id.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QThread>

#include <optional>

namespace qcai2
{

namespace
{

std::optional<debugger_runtime_hooks_t> &debugger_runtime_hooks_storage()
{
    static std::optional<debugger_runtime_hooks_t> hooks;
    return hooks;
}

const debugger_runtime_hooks_t *debugger_runtime_hooks()
{
    const auto &hooks = debugger_runtime_hooks_storage();
    return hooks.has_value() ? &(*hooks) : nullptr;
}

template <typename Result, typename Callable> Result invoke_on_gui_thread(Callable &&callable)
{
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        return callable();
    }

    Result result{};
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [&result, callable = std::forward<Callable>(callable)]() mutable { result = callable(); },
        Qt::BlockingQueuedConnection);
    return result;
}

template <typename Callable> void invoke_void_on_gui_thread(Callable &&callable)
{
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        callable();
        return;
    }

    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [callable = std::forward<Callable>(callable)]() mutable { callable(); },
        Qt::BlockingQueuedConnection);
}

Core::Command *debugger_command(const char *command_id)
{
    if (Core::ActionManager::instance() == nullptr)
    {
        return nullptr;
    }
    return Core::ActionManager::command(Utils::Id::fromName(command_id));
}

QAction *debugger_action(const char *command_id)
{
    if (const auto *hooks = debugger_runtime_hooks();
        hooks != nullptr && hooks->action_for_command)
    {
        return hooks->action_for_command(command_id);
    }

    if (auto *command = debugger_command(command_id); command != nullptr)
    {
        return command->action();
    }
    return nullptr;
}

bool debugger_command_enabled(const char *command_id)
{
    if (auto *action = debugger_action(command_id); action != nullptr)
    {
        return action->isEnabled();
    }
    return false;
}

bool debugger_session_active()
{
    return debugger_command_enabled(Debugger::Constants::STOP) ||
           debugger_command_enabled(Debugger::Constants::INTERRUPT) ||
           debugger_command_enabled(Debugger::Constants::CONTINUE) ||
           debugger_command_enabled(Debugger::Constants::NEXT) ||
           debugger_command_enabled(Debugger::Constants::STEP) ||
           debugger_command_enabled(Debugger::Constants::STEPOUT);
}

bool debugger_session_paused()
{
    return debugger_command_enabled(Debugger::Constants::CONTINUE) ||
           debugger_command_enabled(Debugger::Constants::NEXT) ||
           debugger_command_enabled(Debugger::Constants::STEP) ||
           debugger_command_enabled(Debugger::Constants::STEPOUT);
}

bool debugger_runtime_available_for_mutation()
{
    if (const auto *hooks = debugger_runtime_hooks();
        hooks != nullptr && hooks->mutation_available)
    {
        return hooks->mutation_available();
    }

    return Core::ActionManager::instance() != nullptr;
}

debugger_action_result_t trigger_debugger_command(const char *command_id,
                                                  const QString &success_message)
{
    auto *action = debugger_action(command_id);
    if (action == nullptr)
    {
        return {false, QStringLiteral("Qt Creator debugger action is unavailable.")};
    }
    if (action->isEnabled() == false)
    {
        return {false, QStringLiteral("Debugger action is currently disabled.")};
    }

    action->trigger();
    return {true, success_message};
}

QAbstractItemModel *breakpoint_model()
{
    if (const auto *hooks = debugger_runtime_hooks(); hooks != nullptr && hooks->breakpoint_model)
    {
        return hooks->breakpoint_model();
    }

    return Debugger::Internal::BreakpointManager::model();
}

QString breakpoint_model_string(const QModelIndex &index)
{
    return index.data(Qt::DisplayRole).toString().trimmed();
}

int breakpoint_model_line(const QModelIndex &index)
{
    bool ok = false;
    const int line = index.data(Qt::DisplayRole).toString().trimmed().toInt(&ok);
    return ok == true ? line : -1;
}

bool breakpoint_model_enabled(const QModelIndex &index)
{
    const QVariant check_state = index.data(Qt::CheckStateRole);
    if (check_state.isValid() == false)
    {
        return true;
    }
    return static_cast<Qt::CheckState>(check_state.toInt()) == Qt::Checked;
}

debugger_breakpoint_t breakpoint_to_snapshot(QAbstractItemModel *model, int row)
{
    debugger_breakpoint_t snapshot;
    if (model == nullptr || row < 0 || row >= model->rowCount())
    {
        return snapshot;
    }

    snapshot.model_id = row;
    snapshot.display_name =
        breakpoint_model_string(model->index(row, Debugger::Internal::BreakpointNumberColumn));
    snapshot.function_name =
        breakpoint_model_string(model->index(row, Debugger::Internal::BreakpointFunctionColumn));
    snapshot.file_path =
        breakpoint_model_string(model->index(row, Debugger::Internal::BreakpointFileColumn));
    snapshot.line =
        breakpoint_model_line(model->index(row, Debugger::Internal::BreakpointLineColumn));
    snapshot.condition =
        breakpoint_model_string(model->index(row, Debugger::Internal::BreakpointConditionColumn));
    snapshot.enabled =
        breakpoint_model_enabled(model->index(row, Debugger::Internal::BreakpointNumberColumn));
    snapshot.pending = false;
    if (snapshot.display_name.isEmpty() == true)
    {
        snapshot.display_name =
            snapshot.function_name.isEmpty() == false
                ? snapshot.function_name
                : QStringLiteral("%1:%2").arg(snapshot.file_path).arg(snapshot.line);
    }
    return snapshot;
}

bool managed_breakpoints_equal(const debugger_managed_breakpoint_t &lhs,
                               const debugger_managed_breakpoint_t &rhs)
{
    return lhs.file_path == rhs.file_path && lhs.line == rhs.line &&
           lhs.function_name == rhs.function_name && lhs.condition == rhs.condition;
}

Debugger::Internal::ContextData breakpoint_context(const debugger_managed_breakpoint_t &breakpoint)
{
    Debugger::Internal::ContextData context;
    if (breakpoint.file_path.isEmpty() == false && breakpoint.line > 0)
    {
        context.type = Debugger::Internal::LocationByFile;
        context.fileName = Utils::FilePath::fromString(breakpoint.file_path);
        context.textPosition = {breakpoint.line, 0};
    }
    return context;
}

bool runtime_breakpoint_exists(const debugger_managed_breakpoint_t &breakpoint)
{
    if (const auto *hooks = debugger_runtime_hooks();
        hooks != nullptr && hooks->managed_breakpoint_exists)
    {
        return hooks->managed_breakpoint_exists(breakpoint);
    }

    const Debugger::Internal::ContextData context = breakpoint_context(breakpoint);
    if (context.isValid() == false)
    {
        return false;
    }

    return Debugger::Internal::BreakpointManager::findBreakpointFromContext(context) != nullptr;
}

void toggle_runtime_breakpoint(const debugger_managed_breakpoint_t &breakpoint)
{
    if (const auto *hooks = debugger_runtime_hooks();
        hooks != nullptr && hooks->toggle_managed_breakpoint)
    {
        hooks->toggle_managed_breakpoint(breakpoint);
        return;
    }

    const Debugger::Internal::ContextData context = breakpoint_context(breakpoint);
    if (context.isValid() == true)
    {
        Debugger::Internal::BreakpointManager::setOrRemoveBreakpoint(context);
    }
}

void toggle_runtime_breakpoint_enabled(const debugger_managed_breakpoint_t &breakpoint)
{
    if (const auto *hooks = debugger_runtime_hooks();
        hooks != nullptr && hooks->toggle_managed_breakpoint_enabled)
    {
        hooks->toggle_managed_breakpoint_enabled(breakpoint);
        return;
    }

    const Debugger::Internal::ContextData context = breakpoint_context(breakpoint);
    if (context.isValid() == true)
    {
        Debugger::Internal::BreakpointManager::enableOrDisableBreakpoint(context);
    }
}

int breakpoint_row_for_managed(QAbstractItemModel *model,
                               const debugger_managed_breakpoint_t &breakpoint)
{
    if (model == nullptr || breakpoint.file_path.isEmpty() == true || breakpoint.line <= 0)
    {
        return -1;
    }

    for (int row = 0; row < model->rowCount(); ++row)
    {
        const debugger_breakpoint_t candidate = breakpoint_to_snapshot(model, row);
        if (candidate.file_path == breakpoint.file_path && candidate.line == breakpoint.line)
        {
            return row;
        }
    }

    return -1;
}

}  // namespace

QJsonObject debugger_location_t::to_json() const
{
    return QJsonObject{{QStringLiteral("file_path"), this->file_path},
                       {QStringLiteral("line"), this->line},
                       {QStringLiteral("function_name"), this->function_name},
                       {QStringLiteral("module"), this->module},
                       {QStringLiteral("address"), this->address}};
}

QJsonObject debugger_stack_frame_t::to_json() const
{
    return QJsonObject{{QStringLiteral("index"), this->index},
                       {QStringLiteral("function_name"), this->function_name},
                       {QStringLiteral("file_path"), this->file_path},
                       {QStringLiteral("line"), this->line},
                       {QStringLiteral("module"), this->module},
                       {QStringLiteral("address"), this->address},
                       {QStringLiteral("current"), this->current}};
}

QJsonObject debugger_thread_t::to_json() const
{
    return QJsonObject{{QStringLiteral("id"), this->id},
                       {QStringLiteral("name"), this->name},
                       {QStringLiteral("state"), this->state},
                       {QStringLiteral("function_name"), this->function_name},
                       {QStringLiteral("file_path"), this->file_path},
                       {QStringLiteral("line"), this->line},
                       {QStringLiteral("details"), this->details},
                       {QStringLiteral("current"), this->current}};
}

QJsonObject debugger_variable_t::to_json() const
{
    return QJsonObject{{QStringLiteral("internal_name"), this->internal_name},
                       {QStringLiteral("name"), this->name},
                       {QStringLiteral("expression"), this->expression},
                       {QStringLiteral("value"), this->value},
                       {QStringLiteral("type"), this->type},
                       {QStringLiteral("local"), this->local},
                       {QStringLiteral("watcher"), this->watcher},
                       {QStringLiteral("depth"), this->depth}};
}

QJsonObject debugger_breakpoint_t::to_json() const
{
    return QJsonObject{{QStringLiteral("model_id"), this->model_id},
                       {QStringLiteral("file_path"), this->file_path},
                       {QStringLiteral("line"), this->line},
                       {QStringLiteral("function_name"), this->function_name},
                       {QStringLiteral("condition"), this->condition},
                       {QStringLiteral("enabled"), this->enabled},
                       {QStringLiteral("pending"), this->pending},
                       {QStringLiteral("display_name"), this->display_name}};
}

bool debugger_managed_breakpoint_t::is_valid() const
{
    return (!this->file_path.isEmpty() && this->line > 0) || !this->function_name.isEmpty();
}

QJsonObject debugger_managed_breakpoint_t::to_json() const
{
    return QJsonObject{{QStringLiteral("file_path"), this->file_path},
                       {QStringLiteral("line"), this->line},
                       {QStringLiteral("function_name"), this->function_name},
                       {QStringLiteral("condition"), this->condition},
                       {QStringLiteral("enabled"), this->enabled}};
}

debugger_managed_breakpoint_t debugger_managed_breakpoint_t::from_json(const QJsonObject &obj)
{
    debugger_managed_breakpoint_t breakpoint;
    breakpoint.file_path = obj.value(QStringLiteral("file_path")).toString();
    breakpoint.line = obj.value(QStringLiteral("line")).toInt(-1);
    breakpoint.function_name = obj.value(QStringLiteral("function_name")).toString();
    breakpoint.condition = obj.value(QStringLiteral("condition")).toString();
    breakpoint.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    return breakpoint;
}

QJsonObject debugger_session_snapshot_t::to_json() const
{
    return QJsonObject{{QStringLiteral("active"), this->active},
                       {QStringLiteral("paused"), this->paused},
                       {QStringLiteral("state_name"), this->state_name},
                       {QStringLiteral("engine_display_name"), this->engine_display_name},
                       {QStringLiteral("engine_type"), this->engine_type},
                       {QStringLiteral("location"), this->location.to_json()},
                       {QStringLiteral("stack_frame_count"), this->stack_frame_count},
                       {QStringLiteral("thread_count"), this->thread_count},
                       {QStringLiteral("variable_count"), this->variable_count},
                       {QStringLiteral("breakpoint_count"), this->breakpoint_count}};
}

QJsonObject debugger_action_result_t::to_json() const
{
    return QJsonObject{{QStringLiteral("success"), this->success},
                       {QStringLiteral("message"), this->message}};
}

i_debugger_session_service_t::i_debugger_session_service_t(QObject *parent) : QObject(parent)
{
}

i_debugger_session_service_t::~i_debugger_session_service_t() = default;

debugger_session_service_t::debugger_session_service_t(QObject *parent)
    : i_debugger_session_service_t(parent), debugger_output_buffer(120000)
{
    invoke_void_on_gui_thread([this]() { this->connect_runtime_sources(); });
}

debugger_session_service_t::~debugger_session_service_t()
{
    invoke_void_on_gui_thread([this]() { this->clear_runtime_connections(); });
}

void debugger_session_service_t::set_runtime_hooks_for_testing(
    const debugger_runtime_hooks_t &hooks)
{
    invoke_void_on_gui_thread([hooks]() { debugger_runtime_hooks_storage() = hooks; });
}

void debugger_session_service_t::reset_runtime_hooks_for_testing()
{
    invoke_void_on_gui_thread([]() { debugger_runtime_hooks_storage().reset(); });
}

bool debugger_session_service_t::has_active_session() const
{
    return invoke_on_gui_thread<bool>([]() { return debugger_session_active(); });
}

bool debugger_session_service_t::is_paused() const
{
    return invoke_on_gui_thread<bool>([]() { return debugger_session_paused(); });
}

debugger_session_snapshot_t debugger_session_service_t::session_snapshot() const
{
    return invoke_on_gui_thread<debugger_session_snapshot_t>([]() {
        debugger_session_snapshot_t snapshot;
        snapshot.active = debugger_session_active();
        snapshot.paused = debugger_session_paused();
        snapshot.engine_display_name = QStringLiteral("Qt Creator debugger");
        snapshot.engine_type = QStringLiteral("qtcreator-actions");
        if (snapshot.active == false)
        {
            snapshot.state_name = QStringLiteral("No active debugger session");
        }
        else if (snapshot.paused == true)
        {
            snapshot.state_name = QStringLiteral("Paused");
        }
        else if (debugger_command_enabled(Debugger::Constants::INTERRUPT))
        {
            snapshot.state_name = QStringLiteral("Running");
        }
        else
        {
            snapshot.state_name = QStringLiteral("Active debugger session");
        }

        if (auto *model = breakpoint_model(); model != nullptr)
        {
            snapshot.breakpoint_count = model->rowCount();
        }
        return snapshot;
    });
}

QList<debugger_stack_frame_t> debugger_session_service_t::stack_frames(int max_frames) const
{
    Q_UNUSED(max_frames);
    return {};
}

QList<debugger_thread_t> debugger_session_service_t::threads(int max_threads) const
{
    Q_UNUSED(max_threads);
    return {};
}

QList<debugger_variable_t> debugger_session_service_t::locals(int max_items, int max_depth) const
{
    Q_UNUSED(max_items);
    Q_UNUSED(max_depth);
    return {};
}

QList<debugger_variable_t> debugger_session_service_t::watch_expressions(int max_items,
                                                                         int max_depth) const
{
    Q_UNUSED(max_items);
    Q_UNUSED(max_depth);
    return {};
}

QList<debugger_breakpoint_t> debugger_session_service_t::breakpoints(int max_items) const
{
    return invoke_on_gui_thread<QList<debugger_breakpoint_t>>([max_items]() {
        QList<debugger_breakpoint_t> breakpoints;
        auto *model = breakpoint_model();
        if (model == nullptr)
        {
            return breakpoints;
        }

        const int count = qMin(qMax(0, max_items), model->rowCount());
        for (int row = 0; row < count; ++row)
        {
            breakpoints.append(breakpoint_to_snapshot(model, row));
        }
        return breakpoints;
    });
}

QString debugger_session_service_t::debugger_output_snapshot(int max_lines) const
{
    return invoke_on_gui_thread<QString>([this, max_lines]() {
        if (this->debugger_output_buffer.is_empty() == true)
        {
            return QStringLiteral("No debugger output captured yet.");
        }
        return this->debugger_output_buffer.last_lines(max_lines);
    });
}

QString debugger_session_service_t::debugger_diagnostics_snapshot(int max_items) const
{
    return invoke_on_gui_thread<QString>([this, max_items]() {
        QList<captured_diagnostic_t> diagnostics = this->debugger_diagnostics;
        if (diagnostics.isEmpty() == true)
        {
            diagnostics = extract_diagnostics_from_text(this->debugger_output_buffer.text());
        }
        return format_captured_diagnostics(diagnostics, max_items);
    });
}

debugger_action_result_t debugger_session_service_t::continue_execution()
{
    return invoke_on_gui_thread<debugger_action_result_t>([]() {
        return trigger_debugger_command(
            Debugger::Constants::CONTINUE,
            QStringLiteral("Continue dispatched via Qt Creator action."));
    });
}

debugger_action_result_t debugger_session_service_t::start_debugger()
{
    return invoke_on_gui_thread<debugger_action_result_t>([]() {
        return trigger_debugger_command(
            Debugger::Constants::DEBUGGER_START,
            QStringLiteral("Debugger start dispatched via Qt Creator action."));
    });
}

debugger_action_result_t debugger_session_service_t::step_over()
{
    return invoke_on_gui_thread<debugger_action_result_t>([]() {
        return trigger_debugger_command(
            Debugger::Constants::NEXT,
            QStringLiteral("Step over dispatched via Qt Creator action."));
    });
}

debugger_action_result_t debugger_session_service_t::step_into()
{
    return invoke_on_gui_thread<debugger_action_result_t>([]() {
        return trigger_debugger_command(
            Debugger::Constants::STEP,
            QStringLiteral("Step into dispatched via Qt Creator action."));
    });
}

debugger_action_result_t debugger_session_service_t::step_out()
{
    return invoke_on_gui_thread<debugger_action_result_t>([]() {
        return trigger_debugger_command(
            Debugger::Constants::STEPOUT,
            QStringLiteral("Step out dispatched via Qt Creator action."));
    });
}

debugger_action_result_t debugger_session_service_t::stop_debugger()
{
    return invoke_on_gui_thread<debugger_action_result_t>([]() {
        return trigger_debugger_command(Debugger::Constants::STOP,
                                        QStringLiteral("Stop dispatched via Qt Creator action."));
    });
}

debugger_action_result_t
debugger_session_service_t::set_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint)
{
    return invoke_on_gui_thread<debugger_action_result_t>([this, breakpoint]() {
        if (breakpoint.is_valid() == false)
        {
            return debugger_action_result_t{
                false, QStringLiteral("Breakpoint requires file+line or function_name.")};
        }

        for (const debugger_managed_breakpoint_t &existing : this->persistent_managed_breakpoints)
        {
            if (managed_breakpoints_equal(existing, breakpoint))
            {
                return debugger_action_result_t{
                    false, QStringLiteral("Managed breakpoint already exists.")};
            }
        }

        this->persistent_managed_breakpoints.append(breakpoint);
        this->apply_managed_breakpoints_locked();
        emit this->managed_breakpoints_changed();
        emit this->snapshot_changed();
        return debugger_action_result_t{true, QStringLiteral("Managed breakpoint added.")};
    });
}

debugger_action_result_t debugger_session_service_t::remove_managed_breakpoint(
    const debugger_managed_breakpoint_t &breakpoint)
{
    return invoke_on_gui_thread<debugger_action_result_t>([this, breakpoint]() {
        bool removed = false;
        for (int index = static_cast<int>(this->persistent_managed_breakpoints.size()) - 1;
             index >= 0; --index)
        {
            if (managed_breakpoints_equal(this->persistent_managed_breakpoints.at(index),
                                          breakpoint))
            {
                this->persistent_managed_breakpoints.removeAt(index);
                removed = true;
            }
        }

        const Debugger::Internal::ContextData context = breakpoint_context(breakpoint);
        if (context.isValid() == true && debugger_runtime_available_for_mutation() == true)
        {
            if (runtime_breakpoint_exists(breakpoint))
            {
                toggle_runtime_breakpoint(breakpoint);
            }
        }

        if (removed == false)
        {
            return debugger_action_result_t{false,
                                            QStringLiteral("Managed breakpoint not found.")};
        }

        emit this->managed_breakpoints_changed();
        emit this->snapshot_changed();
        return debugger_action_result_t{true, QStringLiteral("Managed breakpoint removed.")};
    });
}

debugger_action_result_t debugger_session_service_t::set_managed_breakpoint_enabled(
    const debugger_managed_breakpoint_t &breakpoint, bool enabled)
{
    return invoke_on_gui_thread<debugger_action_result_t>([this, breakpoint, enabled]() {
        bool updated = false;
        for (debugger_managed_breakpoint_t &existing : this->persistent_managed_breakpoints)
        {
            if (managed_breakpoints_equal(existing, breakpoint))
            {
                existing.enabled = enabled;
                updated = true;
                break;
            }
        }

        if (updated == false)
        {
            return debugger_action_result_t{false,
                                            QStringLiteral("Managed breakpoint not found.")};
        }

        this->apply_managed_breakpoints_locked();
        emit this->managed_breakpoints_changed();
        emit this->snapshot_changed();
        return debugger_action_result_t{true,
                                        enabled ? QStringLiteral("Managed breakpoint enabled.")
                                                : QStringLiteral("Managed breakpoint disabled.")};
    });
}

QList<debugger_managed_breakpoint_t> debugger_session_service_t::managed_breakpoints() const
{
    return invoke_on_gui_thread<QList<debugger_managed_breakpoint_t>>(
        [this]() { return this->persistent_managed_breakpoints; });
}

void debugger_session_service_t::set_managed_breakpoints(
    const QList<debugger_managed_breakpoint_t> &breakpoints)
{
    invoke_void_on_gui_thread([this, breakpoints]() {
        this->persistent_managed_breakpoints.clear();
        for (const debugger_managed_breakpoint_t &breakpoint : breakpoints)
        {
            if (breakpoint.is_valid() == true)
            {
                this->persistent_managed_breakpoints.append(breakpoint);
            }
        }

        this->apply_managed_breakpoints_locked();
        emit this->managed_breakpoints_changed();
        emit this->snapshot_changed();
    });
}

void debugger_session_service_t::append_debugger_output_for_testing(const QString &text,
                                                                    bool append_newline)
{
    invoke_void_on_gui_thread(
        [this, text, append_newline]() { this->append_debugger_output(text, append_newline); });
}

void debugger_session_service_t::connect_runtime_sources()
{
    this->clear_runtime_connections();

    const auto connect_command = [this](const char *command_id) {
        if (auto *command = debugger_command(command_id); command != nullptr)
        {
            this->runtime_connections.append(
                connect(command, &Core::Command::activeStateChanged, this,
                        &debugger_session_service_t::emit_runtime_snapshot_changed));
            if (QAction *action = command->action(); action != nullptr)
            {
                this->runtime_connections.append(
                    connect(action, &QAction::changed, this,
                            &debugger_session_service_t::emit_runtime_snapshot_changed));
            }
        }
    };

    connect_command(Debugger::Constants::INTERRUPT);
    connect_command(Debugger::Constants::CONTINUE);
    connect_command(Debugger::Constants::STOP);
    connect_command(Debugger::Constants::STEP);
    connect_command(Debugger::Constants::NEXT);
    connect_command(Debugger::Constants::STEPOUT);

    if (auto *model = breakpoint_model(); model != nullptr)
    {
        this->runtime_connections.append(
            connect(model,
                    qOverload<const QModelIndex &, const QModelIndex &, const QList<int> &>(
                        &QAbstractItemModel::dataChanged),
                    this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
                        emit this->snapshot_changed();
                    }));
        this->runtime_connections.append(
            connect(model, &QAbstractItemModel::rowsInserted, this,
                    [this](const QModelIndex &, int, int) { emit this->snapshot_changed(); }));
        this->runtime_connections.append(
            connect(model, &QAbstractItemModel::rowsRemoved, this,
                    [this](const QModelIndex &, int, int) { emit this->snapshot_changed(); }));
        this->runtime_connections.append(
            connect(model, &QAbstractItemModel::modelReset, this,
                    &debugger_session_service_t::emit_runtime_snapshot_changed));
    }
}

void debugger_session_service_t::clear_runtime_connections()
{
    for (const QMetaObject::Connection &connection : this->runtime_connections)
    {
        disconnect(connection);
    }
    this->runtime_connections.clear();
}

void debugger_session_service_t::apply_managed_breakpoints_locked()
{
    if (debugger_runtime_available_for_mutation() == false)
    {
        return;
    }

    auto *model = breakpoint_model();
    for (const debugger_managed_breakpoint_t &managed : this->persistent_managed_breakpoints)
    {
        const Debugger::Internal::ContextData context = breakpoint_context(managed);
        if (context.isValid() == false)
        {
            continue;
        }

        const int existing_row = breakpoint_row_for_managed(model, managed);
        if (existing_row < 0)
        {
            toggle_runtime_breakpoint(managed);
        }

        model = breakpoint_model();
        const int synced_row = breakpoint_row_for_managed(model, managed);
        if (synced_row < 0)
        {
            continue;
        }

        const bool currently_enabled = breakpoint_to_snapshot(model, synced_row).enabled;
        if (currently_enabled != managed.enabled)
        {
            toggle_runtime_breakpoint_enabled(managed);
        }
    }
}

void debugger_session_service_t::append_debugger_output(const QString &text, bool append_newline)
{
    this->debugger_output_buffer.append_chunk(text, append_newline);
    this->debugger_diagnostics =
        extract_diagnostics_from_text(this->debugger_output_buffer.text());
    emit this->debugger_output_changed();
}

void debugger_session_service_t::emit_runtime_snapshot_changed()
{
    emit this->snapshot_changed();
}

QString format_debugger_snapshot_json(const debugger_session_snapshot_t &snapshot)
{
    return QString::fromUtf8(QJsonDocument(snapshot.to_json()).toJson(QJsonDocument::Indented));
}

QString format_debugger_action_result_json(const debugger_action_result_t &result)
{
    return QString::fromUtf8(QJsonDocument(result.to_json()).toJson(QJsonDocument::Indented));
}

}  // namespace qcai2
