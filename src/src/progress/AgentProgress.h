/*! Declares normalized provider-agnostic progress events and shared tool
 *  classification helpers for agent status reporting.
 */
#pragma once

#include <QString>

#include <cstdint>

namespace qcai2
{

enum class provider_raw_event_kind_t : std::uint8_t
{
    REQUEST_STARTED,
    REASONING_DELTA,
    MESSAGE_DELTA,
    TOOL_STARTED,
    TOOL_COMPLETED,
    RESPONSE_COMPLETED,
    IDLE,
    ERROR_EVENT
};

struct provider_raw_event_t
{
    provider_raw_event_kind_t kind = provider_raw_event_kind_t::REQUEST_STARTED;
    QString provider_id;
    QString raw_type;
    QString tool_name;
    QString message;
};

enum class agent_progress_event_kind_t : std::uint8_t
{
    REQUEST_STARTED,
    REASONING_STARTED,
    REASONING_UPDATED,
    TOOL_STARTED,
    TOOL_COMPLETED,
    VALIDATION_STARTED,
    VALIDATION_COMPLETED,
    MESSAGE_DELTA,
    FINAL_ANSWER_STARTED,
    FINAL_ANSWER_COMPLETED,
    IDLE,
    ERROR
};

enum class agent_progress_operation_t : std::uint8_t
{
    NONE,
    EXPLORE,
    SEARCH,
    READ,
    TEST,
    BUILD,
    APPLY_CHANGES,
    GENERIC
};

struct agent_progress_event_t
{
    agent_progress_event_kind_t kind = agent_progress_event_kind_t::IDLE;
    agent_progress_operation_t operation = agent_progress_operation_t::NONE;
    QString provider_id;
    QString raw_type;
    QString tool_name;
    QString label;
    QString message;
};

QString provider_raw_event_kind_name(provider_raw_event_kind_t kind);
QString agent_progress_event_kind_name(agent_progress_event_kind_t kind);
QString agent_progress_operation_name(agent_progress_operation_t operation);
QString prettify_tool_name(const QString &tool_name);
agent_progress_operation_t classify_tool_operation(const QString &tool_name);
QString progress_label_for_tool(const QString &tool_name, agent_progress_operation_t operation);

}  // namespace qcai2
