#include "AgentStatus.h"

namespace qcai2
{

QString agent_status_kind_name(agent_status_kind_t kind)
{
    switch (kind)
    {
        case agent_status_kind_t::IDLE:
            return QStringLiteral("idle");
        case agent_status_kind_t::THINKING:
            return QStringLiteral("thinking");
        case agent_status_kind_t::EXPLORING:
            return QStringLiteral("exploring");
        case agent_status_kind_t::SEARCHING:
            return QStringLiteral("searching");
        case agent_status_kind_t::READING:
            return QStringLiteral("reading");
        case agent_status_kind_t::RUNNING_TOOL:
            return QStringLiteral("running");
        case agent_status_kind_t::VALIDATING:
            return QStringLiteral("validating");
        case agent_status_kind_t::TESTING:
            return QStringLiteral("testing");
        case agent_status_kind_t::BUILDING:
            return QStringLiteral("building");
        case agent_status_kind_t::APPLYING_CHANGES:
            return QStringLiteral("applying_changes");
        case agent_status_kind_t::PREPARING_FINAL_ANSWER:
            return QStringLiteral("preparing_final_answer");
        case agent_status_kind_t::DONE:
            return QStringLiteral("done");
        case agent_status_kind_t::ERROR:
            return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

bool agent_status_classifier_t::apply(const agent_progress_event_t &event,
                                      agent_status_snapshot_t *changed_status)
{
    switch (event.kind)
    {
        case agent_progress_event_kind_t::REQUEST_STARTED:
        case agent_progress_event_kind_t::REASONING_STARTED:
        case agent_progress_event_kind_t::REASONING_UPDATED:
        case agent_progress_event_kind_t::MESSAGE_DELTA:
            if (this->current.kind == agent_status_kind_t::VALIDATING)
            {
                return false;
            }
            return this->set_status({agent_status_kind_t::THINKING, {}, {}}, changed_status);

        case agent_progress_event_kind_t::TOOL_STARTED:
            return this->set_status(this->tool_status(event), changed_status);

        case agent_progress_event_kind_t::TOOL_COMPLETED:
        case agent_progress_event_kind_t::VALIDATION_COMPLETED:
        case agent_progress_event_kind_t::IDLE:
            return false;

        case agent_progress_event_kind_t::VALIDATION_STARTED:
            return this->set_status({agent_status_kind_t::VALIDATING, event.label, {}},
                                    changed_status);

        case agent_progress_event_kind_t::FINAL_ANSWER_STARTED:
            return this->set_status({agent_status_kind_t::PREPARING_FINAL_ANSWER, {}, {}},
                                    changed_status);

        case agent_progress_event_kind_t::FINAL_ANSWER_COMPLETED:
            return this->set_status({agent_status_kind_t::DONE, {}, {}}, changed_status);

        case agent_progress_event_kind_t::ERROR:
            return this->set_status({agent_status_kind_t::ERROR, {}, event.message},
                                    changed_status);
    }

    return false;
}

agent_status_snapshot_t agent_status_classifier_t::current_status() const
{
    return this->current;
}

bool agent_status_classifier_t::set_status(const agent_status_snapshot_t &status,
                                           agent_status_snapshot_t *changed_status)
{
    if (this->current.kind == status.kind && this->current.subject == status.subject &&
        this->current.message == status.message)
    {
        return false;
    }

    this->current = status;
    if (changed_status != nullptr)
    {
        *changed_status = status;
    }
    return true;
}

agent_status_snapshot_t
agent_status_classifier_t::tool_status(const agent_progress_event_t &event) const
{
    switch (event.operation)
    {
        case agent_progress_operation_t::EXPLORE:
            return {agent_status_kind_t::EXPLORING, event.label, {}};
        case agent_progress_operation_t::SEARCH:
            return {agent_status_kind_t::SEARCHING, event.label, {}};
        case agent_progress_operation_t::READ:
            return {agent_status_kind_t::READING, event.label, {}};
        case agent_progress_operation_t::TEST:
            return {agent_status_kind_t::TESTING, event.label, {}};
        case agent_progress_operation_t::BUILD:
            return {agent_status_kind_t::BUILDING, event.label, {}};
        case agent_progress_operation_t::APPLY_CHANGES:
            return {agent_status_kind_t::APPLYING_CHANGES, event.label, {}};
        case agent_progress_operation_t::GENERIC:
        case agent_progress_operation_t::NONE:
            break;
    }

    return {agent_status_kind_t::RUNNING_TOOL, event.label, {}};
}

}  // namespace qcai2
