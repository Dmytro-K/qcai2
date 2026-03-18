#include "AgentStatusFormatter.h"

namespace qcai2
{

namespace
{

QString with_subject(const QString &verb, const QString &subject)
{
    if (true == subject.isEmpty())
    {
        return verb;
    }

    return QStringLiteral("%1 %2").arg(verb, subject);
}

}  // namespace

QString format_agent_status(const agent_status_snapshot_t &status)
{
    switch (status.kind)
    {
        case agent_status_kind_t::IDLE:
            return QStringLiteral("Idle");
        case agent_status_kind_t::THINKING:
            return QStringLiteral("Thinking");
        case agent_status_kind_t::EXPLORING:
            return with_subject(QStringLiteral("Exploring"), status.subject);
        case agent_status_kind_t::SEARCHING:
            return with_subject(QStringLiteral("Searching"), status.subject);
        case agent_status_kind_t::READING:
            return with_subject(QStringLiteral("Reading"), status.subject);
        case agent_status_kind_t::RUNNING_TOOL:
            return with_subject(QStringLiteral("Running"), status.subject.isEmpty() == false
                                                               ? status.subject
                                                               : QStringLiteral("tool"));
        case agent_status_kind_t::VALIDATING:
            return with_subject(QStringLiteral("Validating"), status.subject);
        case agent_status_kind_t::TESTING:
            return with_subject(QStringLiteral("Testing"), status.subject);
        case agent_status_kind_t::BUILDING:
            return with_subject(QStringLiteral("Building"), status.subject);
        case agent_status_kind_t::APPLYING_CHANGES:
            return QStringLiteral("Applying changes");
        case agent_status_kind_t::PREPARING_FINAL_ANSWER:
            return QStringLiteral("Preparing final answer");
        case agent_status_kind_t::DONE:
            return QStringLiteral("Done");
        case agent_status_kind_t::ERROR:
            return status.message.isEmpty() == true
                       ? QStringLiteral("Error")
                       : QStringLiteral("Error: %1").arg(status.message);
    }

    return QStringLiteral("Idle");
}

}  // namespace qcai2
