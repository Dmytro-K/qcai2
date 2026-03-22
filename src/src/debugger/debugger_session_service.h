/*! @file
    @brief Declares debugger session snapshots and the shared debugger integration service.
*/
#pragma once

#include "../util/output_capture_support.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class QAction;
class QAbstractItemModel;

namespace qcai2
{

struct debugger_location_t
{
    QString file_path;
    int line = -1;
    QString function_name;
    QString module;
    QString address;

    QJsonObject to_json() const;
};

struct debugger_stack_frame_t
{
    int index = -1;
    QString function_name;
    QString file_path;
    int line = -1;
    QString module;
    QString address;
    bool current = false;

    QJsonObject to_json() const;
};

struct debugger_thread_t
{
    QString id;
    QString name;
    QString state;
    QString function_name;
    QString file_path;
    int line = -1;
    QString details;
    bool current = false;

    QJsonObject to_json() const;
};

struct debugger_variable_t
{
    QString internal_name;
    QString name;
    QString expression;
    QString value;
    QString type;
    bool local = false;
    bool watcher = false;
    int depth = 0;

    QJsonObject to_json() const;
};

struct debugger_breakpoint_t
{
    int model_id = -1;
    QString file_path;
    int line = -1;
    QString function_name;
    QString condition;
    bool enabled = true;
    bool pending = false;
    QString display_name;

    QJsonObject to_json() const;
};

struct debugger_managed_breakpoint_t
{
    QString file_path;
    int line = -1;
    QString function_name;
    QString condition;
    bool enabled = true;

    bool is_valid() const;
    QJsonObject to_json() const;
    static debugger_managed_breakpoint_t from_json(const QJsonObject &obj);
};

struct debugger_session_snapshot_t
{
    bool active = false;
    bool paused = false;
    QString state_name;
    QString engine_display_name;
    QString engine_type;
    debugger_location_t location;
    int stack_frame_count = 0;
    int thread_count = 0;
    int variable_count = 0;
    int breakpoint_count = 0;

    QJsonObject to_json() const;
};

struct debugger_action_result_t
{
    bool success = false;
    QString message;

    QJsonObject to_json() const;
};

struct debugger_runtime_hooks_t
{
    std::function<QAction *(const char *command_id)> action_for_command;
    std::function<QAbstractItemModel *()> breakpoint_model;
    std::function<bool()> mutation_available;
    std::function<bool(const debugger_managed_breakpoint_t &breakpoint)> managed_breakpoint_exists;
    std::function<void(const debugger_managed_breakpoint_t &breakpoint)> toggle_managed_breakpoint;
    std::function<void(const debugger_managed_breakpoint_t &breakpoint)>
        toggle_managed_breakpoint_enabled;
};

class i_debugger_session_service_t : public QObject
{
    Q_OBJECT

public:
    explicit i_debugger_session_service_t(QObject *parent = nullptr);
    ~i_debugger_session_service_t() override;

    virtual bool has_active_session() const = 0;
    virtual bool is_paused() const = 0;
    virtual debugger_session_snapshot_t session_snapshot() const = 0;
    virtual QList<debugger_stack_frame_t> stack_frames(int max_frames = 50) const = 0;
    virtual QList<debugger_thread_t> threads(int max_threads = 100) const = 0;
    virtual QList<debugger_variable_t> locals(int max_items = 100, int max_depth = 1) const = 0;
    virtual QList<debugger_variable_t> watch_expressions(int max_items = 100,
                                                         int max_depth = 1) const = 0;
    virtual QList<debugger_breakpoint_t> breakpoints(int max_items = 200) const = 0;
    virtual QString debugger_output_snapshot(int max_lines = 200) const = 0;
    virtual QString debugger_diagnostics_snapshot(int max_items = 50) const = 0;

    virtual debugger_action_result_t start_debugger() = 0;
    virtual debugger_action_result_t continue_execution() = 0;
    virtual debugger_action_result_t step_over() = 0;
    virtual debugger_action_result_t step_into() = 0;
    virtual debugger_action_result_t step_out() = 0;
    virtual debugger_action_result_t stop_debugger() = 0;

    virtual debugger_action_result_t
    set_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint) = 0;
    virtual debugger_action_result_t
    remove_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint) = 0;
    virtual debugger_action_result_t
    set_managed_breakpoint_enabled(const debugger_managed_breakpoint_t &breakpoint,
                                   bool enabled) = 0;
    virtual QList<debugger_managed_breakpoint_t> managed_breakpoints() const = 0;
    virtual void
    set_managed_breakpoints(const QList<debugger_managed_breakpoint_t> &breakpoints) = 0;

signals:
    void snapshot_changed();
    void debugger_output_changed();
    void managed_breakpoints_changed();
};

class debugger_session_service_t final : public i_debugger_session_service_t
{
    Q_OBJECT

public:
    explicit debugger_session_service_t(QObject *parent = nullptr);
    ~debugger_session_service_t() override;

    bool has_active_session() const override;
    bool is_paused() const override;
    debugger_session_snapshot_t session_snapshot() const override;
    QList<debugger_stack_frame_t> stack_frames(int max_frames = 50) const override;
    QList<debugger_thread_t> threads(int max_threads = 100) const override;
    QList<debugger_variable_t> locals(int max_items = 100, int max_depth = 1) const override;
    QList<debugger_variable_t> watch_expressions(int max_items = 100,
                                                 int max_depth = 1) const override;
    QList<debugger_breakpoint_t> breakpoints(int max_items = 200) const override;
    QString debugger_output_snapshot(int max_lines = 200) const override;
    QString debugger_diagnostics_snapshot(int max_items = 50) const override;

    debugger_action_result_t start_debugger() override;
    debugger_action_result_t continue_execution() override;
    debugger_action_result_t step_over() override;
    debugger_action_result_t step_into() override;
    debugger_action_result_t step_out() override;
    debugger_action_result_t stop_debugger() override;

    debugger_action_result_t
    set_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint) override;
    debugger_action_result_t
    remove_managed_breakpoint(const debugger_managed_breakpoint_t &breakpoint) override;
    debugger_action_result_t
    set_managed_breakpoint_enabled(const debugger_managed_breakpoint_t &breakpoint,
                                   bool enabled) override;
    QList<debugger_managed_breakpoint_t> managed_breakpoints() const override;
    void set_managed_breakpoints(const QList<debugger_managed_breakpoint_t> &breakpoints) override;

    void append_debugger_output_for_testing(const QString &text, bool append_newline = true);
    static void set_runtime_hooks_for_testing(const debugger_runtime_hooks_t &hooks);
    static void reset_runtime_hooks_for_testing();

private:
    void connect_runtime_sources();
    void clear_runtime_connections();
    void apply_managed_breakpoints_locked();
    void append_debugger_output(const QString &text, bool append_newline);
    void emit_runtime_snapshot_changed();

    bounded_text_buffer_t debugger_output_buffer;
    QList<captured_diagnostic_t> debugger_diagnostics;
    QList<debugger_managed_breakpoint_t> persistent_managed_breakpoints;
    QList<QMetaObject::Connection> runtime_connections;
};

QString format_debugger_snapshot_json(const debugger_session_snapshot_t &snapshot);
QString format_debugger_action_result_json(const debugger_action_result_t &result);

}  // namespace qcai2
