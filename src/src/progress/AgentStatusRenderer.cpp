#include "AgentStatusRenderer.h"

#include "AgentStatusFormatter.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace qcai2
{

agent_status_renderer_t::agent_status_renderer_t(agent_status_render_mode_t mode) : mode(mode)
{
}

agent_status_render_update_t agent_status_renderer_t::render(const agent_status_snapshot_t &status)
{
    agent_status_render_update_t update;
    const QString text = format_agent_status(status);

    if (text == this->last_rendered_text)
    {
        return update;
    }

    this->last_rendered_text = text;
    update.status_changed = true;
    update.status_text = text;

    if (this->mode == agent_status_render_mode_t::NON_INTERACTIVE)
    {
        QJsonObject root{{QStringLiteral("type"), QStringLiteral("agent_status")},
                         {QStringLiteral("phase"), agent_status_kind_name(status.kind)},
                         {QStringLiteral("text"), text}};
        if (status.subject.isEmpty() == false)
        {
            root.insert(QStringLiteral("subject"), status.subject);
        }
        if (status.message.isEmpty() == false)
        {
            root.insert(QStringLiteral("message"), status.message);
        }
        update.stable_log_line =
            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    return update;
}

QString agent_status_renderer_t::current_text() const
{
    return this->last_rendered_text;
}

}  // namespace qcai2
