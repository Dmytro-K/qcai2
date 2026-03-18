#include "AgentProgressTracker.h"

#include "AgentStatusFormatter.h"

namespace qcai2
{

AgentProgressTracker::AgentProgressTracker(const QString &providerId, AgentStatusRenderMode mode,
                                           bool verbose)
    : m_adapter(createProviderProgressAdapter(providerId)), m_renderer(mode), m_verbose(verbose)
{
}

AgentProgressRenderResult
AgentProgressTracker::handleProviderRawEvent(const ProviderRawEvent &event)
{
    AgentProgressRenderResult result;
    const QList<AgentProgressEvent> normalizedEvents = m_adapter->adapt(event);
    for (const AgentProgressEvent &normalizedEvent : normalizedEvents)
    {
        result = merge(result, applyNormalizedEvent(normalizedEvent, &event));
    }
    return result;
}

AgentProgressRenderResult
AgentProgressTracker::handleNormalizedEvent(const AgentProgressEvent &event)
{
    return applyNormalizedEvent(event, nullptr);
}

QString AgentProgressTracker::currentStatusText() const
{
    return m_renderer.currentText();
}

AgentProgressRenderResult AgentProgressTracker::merge(const AgentProgressRenderResult &lhs,
                                                      const AgentProgressRenderResult &rhs) const
{
    AgentProgressRenderResult merged = lhs;
    if (rhs.statusChanged == true)
    {
        merged.statusChanged = true;
        merged.statusText = rhs.statusText;
    }
    if (rhs.stableLogLine.isEmpty() == false)
    {
        merged.stableLogLine = rhs.stableLogLine;
    }
    merged.debugLines.append(rhs.debugLines);
    return merged;
}

AgentProgressRenderResult
AgentProgressTracker::applyNormalizedEvent(const AgentProgressEvent &event,
                                           const ProviderRawEvent *rawEvent)
{
    AgentProgressRenderResult result;
    AgentStatusSnapshot changedStatus;
    const bool statusChanged = m_classifier.apply(event, &changedStatus);
    if (statusChanged == true)
    {
        const AgentStatusRenderUpdate renderUpdate = m_renderer.render(changedStatus);
        result.statusChanged = renderUpdate.statusChanged;
        result.statusText = renderUpdate.statusText;
        result.stableLogLine = renderUpdate.stableLogLine;
    }

    if (m_verbose == true)
    {
        const QString rawType = (rawEvent != nullptr) ? rawEvent->rawType : event.rawType;
        result.debugLines.append(
            QStringLiteral("raw=%1 normalized=%2 status=%3")
                .arg(rawType.isEmpty() == false ? rawType : QStringLiteral("controller"),
                     agentProgressEventKindName(event.kind),
                     formatAgentStatus(m_classifier.currentStatus())));
    }

    return result;
}

}  // namespace qcai2
