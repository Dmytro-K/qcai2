#include "AgentProgress.h"

namespace qcai2
{

namespace
{

QString normalized_tool_name(QString tool_name)
{
    tool_name = tool_name.trimmed().toLower();
    tool_name.replace(QLatin1Char('-'), QLatin1Char('_'));
    tool_name.replace(QLatin1Char(' '), QLatin1Char('_'));
    return tool_name;
}

}  // namespace

QString provider_raw_event_kind_name(provider_raw_event_kind_t kind)
{
    switch (kind)
    {
        case provider_raw_event_kind_t::REQUEST_STARTED:
            return QStringLiteral("request_started");
        case provider_raw_event_kind_t::REASONING_DELTA:
            return QStringLiteral("reasoning_delta");
        case provider_raw_event_kind_t::MESSAGE_DELTA:
            return QStringLiteral("message_delta");
        case provider_raw_event_kind_t::TOOL_STARTED:
            return QStringLiteral("tool_started");
        case provider_raw_event_kind_t::TOOL_COMPLETED:
            return QStringLiteral("tool_completed");
        case provider_raw_event_kind_t::RESPONSE_COMPLETED:
            return QStringLiteral("response_completed");
        case provider_raw_event_kind_t::IDLE:
            return QStringLiteral("idle");
        case provider_raw_event_kind_t::ERROR_EVENT:
            return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

QString agent_progress_event_kind_name(agent_progress_event_kind_t kind)
{
    switch (kind)
    {
        case agent_progress_event_kind_t::REQUEST_STARTED:
            return QStringLiteral("request_started");
        case agent_progress_event_kind_t::REASONING_STARTED:
            return QStringLiteral("reasoning_started");
        case agent_progress_event_kind_t::REASONING_UPDATED:
            return QStringLiteral("reasoning_updated");
        case agent_progress_event_kind_t::TOOL_STARTED:
            return QStringLiteral("tool_started");
        case agent_progress_event_kind_t::TOOL_COMPLETED:
            return QStringLiteral("tool_completed");
        case agent_progress_event_kind_t::VALIDATION_STARTED:
            return QStringLiteral("validation_started");
        case agent_progress_event_kind_t::VALIDATION_COMPLETED:
            return QStringLiteral("validation_completed");
        case agent_progress_event_kind_t::MESSAGE_DELTA:
            return QStringLiteral("message_delta");
        case agent_progress_event_kind_t::FINAL_ANSWER_STARTED:
            return QStringLiteral("final_answer_started");
        case agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED:
            return QStringLiteral("final_answer_completed");
        case agent_progress_event_kind_t::IDLE:
            return QStringLiteral("idle");
        case agent_progress_event_kind_t::ERROR:
            return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

QString agent_progress_operation_name(agent_progress_operation_t operation)
{
    switch (operation)
    {
        case agent_progress_operation_t::NONE:
            return QStringLiteral("none");
        case agent_progress_operation_t::EXPLORE:
            return QStringLiteral("explore");
        case agent_progress_operation_t::SEARCH:
            return QStringLiteral("search");
        case agent_progress_operation_t::READ:
            return QStringLiteral("read");
        case agent_progress_operation_t::TEST:
            return QStringLiteral("test");
        case agent_progress_operation_t::BUILD:
            return QStringLiteral("build");
        case agent_progress_operation_t::APPLY_CHANGES:
            return QStringLiteral("apply_changes");
        case agent_progress_operation_t::GENERIC:
            return QStringLiteral("generic");
    }

    return QStringLiteral("unknown");
}

QString prettify_tool_name(const QString &tool_name)
{
    QString pretty = normalized_tool_name(tool_name);
    pretty.replace(QLatin1Char('_'), QLatin1Char(' '));
    return pretty.trimmed();
}

agent_progress_operation_t classify_tool_operation(const QString &tool_name)
{
    const QString normalized = normalized_tool_name(tool_name);

    if (normalized.contains(QStringLiteral("compact")) == true)
    {
        return agent_progress_operation_t::EXPLORE;
    }
    if (normalized.contains(QStringLiteral("search")) == true ||
        normalized.contains(QStringLiteral("grep")) == true ||
        normalized.contains(QStringLiteral("find")) == true)
    {
        return agent_progress_operation_t::SEARCH;
    }
    if (normalized.contains(QStringLiteral("read")) == true)
    {
        return agent_progress_operation_t::READ;
    }
    if (normalized.contains(QStringLiteral("test")) == true)
    {
        return agent_progress_operation_t::TEST;
    }
    if (normalized.contains(QStringLiteral("build")) == true)
    {
        return agent_progress_operation_t::BUILD;
    }
    if (normalized.contains(QStringLiteral("apply_patch")) == true ||
        normalized.contains(QStringLiteral("edit")) == true ||
        normalized.contains(QStringLiteral("write_file")) == true ||
        normalized.contains(QStringLiteral("create_file")) == true)
    {
        return agent_progress_operation_t::APPLY_CHANGES;
    }

    return agent_progress_operation_t::GENERIC;
}

QString progress_label_for_tool(const QString &tool_name, agent_progress_operation_t operation)
{
    const QString normalized = normalized_tool_name(tool_name);

    if (normalized.contains(QStringLiteral("compact")) == true)
    {
        return QStringLiteral("compact command");
    }
    if (operation == agent_progress_operation_t::SEARCH &&
        normalized == QStringLiteral("search_repo"))
    {
        return QStringLiteral("repository");
    }
    if (operation == agent_progress_operation_t::READ && normalized == QStringLiteral("read_file"))
    {
        return QStringLiteral("file");
    }
    if (operation == agent_progress_operation_t::BUILD ||
        operation == agent_progress_operation_t::TEST ||
        operation == agent_progress_operation_t::APPLY_CHANGES)
    {
        return {};
    }

    return prettify_tool_name(tool_name);
}

}  // namespace qcai2
