#include "AgentStatusFormatter.h"

namespace qcai2
{

namespace
{

QString withSubject(const QString &verb, const QString &subject)
{
    if (subject.isEmpty() == true)
    {
        return verb;
    }

    return QStringLiteral("%1 %2").arg(verb, subject);
}

}  // namespace

QString formatAgentStatus(const AgentStatusSnapshot &status)
{
    switch (status.kind)
    {
        case AgentStatusKind::Idle:
            return QStringLiteral("Idle");
        case AgentStatusKind::Thinking:
            return QStringLiteral("Thinking");
        case AgentStatusKind::Exploring:
            return withSubject(QStringLiteral("Exploring"), status.subject);
        case AgentStatusKind::Searching:
            return withSubject(QStringLiteral("Searching"), status.subject);
        case AgentStatusKind::Reading:
            return withSubject(QStringLiteral("Reading"), status.subject);
        case AgentStatusKind::RunningTool:
            return withSubject(QStringLiteral("Running"), status.subject.isEmpty() == false
                                                              ? status.subject
                                                              : QStringLiteral("tool"));
        case AgentStatusKind::Validating:
            return withSubject(QStringLiteral("Validating"), status.subject);
        case AgentStatusKind::Testing:
            return withSubject(QStringLiteral("Testing"), status.subject);
        case AgentStatusKind::Building:
            return withSubject(QStringLiteral("Building"), status.subject);
        case AgentStatusKind::ApplyingChanges:
            return QStringLiteral("Applying changes");
        case AgentStatusKind::PreparingFinalAnswer:
            return QStringLiteral("Preparing final answer");
        case AgentStatusKind::Done:
            return QStringLiteral("Done");
        case AgentStatusKind::Error:
            return status.message.isEmpty() == true
                       ? QStringLiteral("Error")
                       : QStringLiteral("Error: %1").arg(status.message);
    }

    return QStringLiteral("Idle");
}

}  // namespace qcai2
