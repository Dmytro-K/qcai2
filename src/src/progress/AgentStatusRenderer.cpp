#include "AgentStatusRenderer.h"

#include "AgentStatusFormatter.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace qcai2
{

AgentStatusRenderer::AgentStatusRenderer(AgentStatusRenderMode mode) : m_mode(mode)
{
}

AgentStatusRenderUpdate AgentStatusRenderer::render(const AgentStatusSnapshot &status)
{
    AgentStatusRenderUpdate update;
    const QString text = formatAgentStatus(status);

    if (text == m_lastRenderedText)
    {
        return update;
    }

    m_lastRenderedText = text;
    update.statusChanged = true;
    update.statusText = text;

    if (m_mode == AgentStatusRenderMode::NonInteractive)
    {
        QJsonObject root{{QStringLiteral("type"), QStringLiteral("agent_status")},
                         {QStringLiteral("phase"), agentStatusKindName(status.kind)},
                         {QStringLiteral("text"), text}};
        if (status.subject.isEmpty() == false)
        {
            root.insert(QStringLiteral("subject"), status.subject);
        }
        if (status.message.isEmpty() == false)
        {
            root.insert(QStringLiteral("message"), status.message);
        }
        update.stableLogLine =
            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    return update;
}

QString AgentStatusRenderer::currentText() const
{
    return m_lastRenderedText;
}

}  // namespace qcai2
