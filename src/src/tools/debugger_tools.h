/*! @file
    @brief Declares debugger inspection and control tools exposed to the agent.
*/
#pragma once

#include "../debugger/debugger_session_service.h"
#include "i_tool.h"
#include "tool_registry.h"

namespace qcai2
{

class debugger_tool_base_t : public i_tool_t
{
public:
    explicit debugger_tool_base_t(i_debugger_session_service_t *debugger_service);

protected:
    i_debugger_session_service_t *debugger_service = nullptr;
};

class get_debug_session_state_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class get_stack_frames_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class get_threads_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class get_locals_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class get_watch_expressions_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class get_breakpoints_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class show_debugger_console_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
};

class debugger_continue_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class debugger_start_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class debugger_step_over_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class debugger_step_into_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class debugger_step_out_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class debugger_stop_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class set_breakpoint_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class remove_breakpoint_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class enable_breakpoint_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

class disable_breakpoint_tool_t final : public debugger_tool_base_t
{
public:
    using debugger_tool_base_t::debugger_tool_base_t;
    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;
    bool requires_approval() const override;
};

void register_debugger_tools(tool_registry_t *registry,
                             i_debugger_session_service_t *debugger_service);

}  // namespace qcai2
