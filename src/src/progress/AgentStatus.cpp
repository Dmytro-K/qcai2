#include "AgentStatus.h"

namespace qcai2
{

QString agentStatusKindName(AgentStatusKind kind)
{
    switch (kind)
    {
        case AgentStatusKind::Idle:
            return QStringLiteral("idle");
        case AgentStatusKind::Thinking:
            return QStringLiteral("thinking");
        case AgentStatusKind::Exploring:
            return QStringLiteral("exploring");
        case AgentStatusKind::Searching:
            return QStringLiteral("searching");
        case AgentStatusKind::Reading:
            return QStringLiteral("reading");
        case AgentStatusKind::RunningTool:
            return QStringLiteral("running");
        case AgentStatusKind::Validating:
            return QStringLiteral("validating");
        case AgentStatusKind::Testing:
            return QStringLiteral("testing");
        case AgentStatusKind::Building:
            return QStringLiteral("building");
        case AgentStatusKind::ApplyingChanges:
            return QStringLiteral("applying_changes");
        case AgentStatusKind::PreparingFinalAnswer:
            return QStringLiteral("preparing_final_answer");
        case AgentStatusKind::Done:
            return QStringLiteral("done");
        case AgentStatusKind::Error:
            return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

bool AgentStatusClassifier::apply(const AgentProgressEvent &event,
                                  AgentStatusSnapshot *changedStatus)
{
    switch (event.kind)
    {
        case AgentProgressEventKind::RequestStarted:
        case AgentProgressEventKind::ReasoningStarted:
        case AgentProgressEventKind::ReasoningUpdated:
        case AgentProgressEventKind::MessageDelta:
            if (m_current.kind == AgentStatusKind::Validating)
            {
                return false;
            }
            return setStatus({AgentStatusKind::Thinking, {}, {}}, changedStatus);

        case AgentProgressEventKind::ToolStarted:
            return setStatus(toolStatus(event), changedStatus);

        case AgentProgressEventKind::ToolCompleted:
        case AgentProgressEventKind::ValidationCompleted:
        case AgentProgressEventKind::Idle:
            return false;

        case AgentProgressEventKind::ValidationStarted:
            return setStatus({AgentStatusKind::Validating, event.label, {}}, changedStatus);

        case AgentProgressEventKind::FinalAnswerStarted:
            return setStatus({AgentStatusKind::PreparingFinalAnswer, {}, {}}, changedStatus);

        case AgentProgressEventKind::FinalAnswerCompleted:
            return setStatus({AgentStatusKind::Done, {}, {}}, changedStatus);

        case AgentProgressEventKind::Error:
            return setStatus({AgentStatusKind::Error, {}, event.message}, changedStatus);
    }

    return false;
}

AgentStatusSnapshot AgentStatusClassifier::currentStatus() const
{
    return m_current;
}

bool AgentStatusClassifier::setStatus(const AgentStatusSnapshot &status,
                                      AgentStatusSnapshot *changedStatus)
{
    if (m_current.kind == status.kind && m_current.subject == status.subject &&
        m_current.message == status.message)
    {
        return false;
    }

    m_current = status;
    if (changedStatus != nullptr)
    {
        *changedStatus = status;
    }
    return true;
}

AgentStatusSnapshot AgentStatusClassifier::toolStatus(const AgentProgressEvent &event) const
{
    switch (event.operation)
    {
        case AgentProgressOperation::Explore:
            return {AgentStatusKind::Exploring, event.label, {}};
        case AgentProgressOperation::Search:
            return {AgentStatusKind::Searching, event.label, {}};
        case AgentProgressOperation::Read:
            return {AgentStatusKind::Reading, event.label, {}};
        case AgentProgressOperation::Test:
            return {AgentStatusKind::Testing, event.label, {}};
        case AgentProgressOperation::Build:
            return {AgentStatusKind::Building, event.label, {}};
        case AgentProgressOperation::ApplyChanges:
            return {AgentStatusKind::ApplyingChanges, event.label, {}};
        case AgentProgressOperation::Generic:
        case AgentProgressOperation::None:
            break;
    }

    return {AgentStatusKind::RunningTool, event.label, {}};
}

}  // namespace qcai2
