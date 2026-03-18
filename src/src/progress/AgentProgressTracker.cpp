#include "AgentProgressTracker.h"

#include "AgentStatusFormatter.h"

namespace qcai2
{

agent_progress_tracker_t::agent_progress_tracker_t(const QString &provider_id,
                                                   agent_status_render_mode_t mode, bool verbose)
    : adapter(create_provider_progress_adapter(provider_id)), renderer(mode), verbose(verbose)
{
}

agent_progress_render_result_t
agent_progress_tracker_t::handle_provider_raw_event(const provider_raw_event_t &event)
{
    agent_progress_render_result_t result;
    const QList<agent_progress_event_t> normalized_events = this->adapter->adapt(event);
    for (const agent_progress_event_t &normalized_event : normalized_events)
    {
        result = this->merge(result, this->apply_normalized_event(normalized_event, &event));
    }
    return result;
}

agent_progress_render_result_t
agent_progress_tracker_t::handle_normalized_event(const agent_progress_event_t &event)
{
    return this->apply_normalized_event(event, nullptr);
}

QString agent_progress_tracker_t::current_status_text() const
{
    return this->renderer.current_text();
}

agent_progress_render_result_t
agent_progress_tracker_t::merge(const agent_progress_render_result_t &lhs,
                                const agent_progress_render_result_t &rhs) const
{
    agent_progress_render_result_t merged = lhs;
    if (rhs.status_changed == true)
    {
        merged.status_changed = true;
        merged.status_text = rhs.status_text;
    }
    if (rhs.stable_log_line.isEmpty() == false)
    {
        merged.stable_log_line = rhs.stable_log_line;
    }
    merged.debug_lines.append(rhs.debug_lines);
    return merged;
}

agent_progress_render_result_t
agent_progress_tracker_t::apply_normalized_event(const agent_progress_event_t &event,
                                                 const provider_raw_event_t *raw_event)
{
    agent_progress_render_result_t result;
    agent_status_snapshot_t changed_status;
    const bool status_changed = this->classifier.apply(event, &changed_status);
    if (status_changed == true)
    {
        const agent_status_render_update_t render_update = this->renderer.render(changed_status);
        result.status_changed = render_update.status_changed;
        result.status_text = render_update.status_text;
        result.stable_log_line = render_update.stable_log_line;
    }

    if (this->verbose == true)
    {
        const QString raw_type = (raw_event != nullptr) ? raw_event->raw_type : event.raw_type;
        result.debug_lines.append(
            QStringLiteral("raw=%1 normalized=%2 status=%3")
                .arg(raw_type.isEmpty() == false ? raw_type : QStringLiteral("controller"),
                     agent_progress_event_kind_name(event.kind),
                     format_agent_status(this->classifier.current_status())));
    }

    return result;
}

}  // namespace qcai2
