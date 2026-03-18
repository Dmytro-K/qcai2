/*! Declares the progress tracker that glues provider raw events, normalized
 *  events, semantic status classification, and rendering together.
 */
#pragma once

#include "AgentStatusRenderer.h"
#include "ProviderProgressAdapter.h"

#include <QStringList>

#include <memory>

namespace qcai2
{

struct agent_progress_render_result_t
{
    bool status_changed = false;
    QString status_text;
    QString stable_log_line;
    QStringList debug_lines;
};

class agent_progress_tracker_t
{
public:
    agent_progress_tracker_t(const QString &provider_id, agent_status_render_mode_t mode,
                             bool verbose);

    agent_progress_render_result_t handle_provider_raw_event(const provider_raw_event_t &event);
    agent_progress_render_result_t handle_normalized_event(const agent_progress_event_t &event);
    QString current_status_text() const;

private:
    agent_progress_render_result_t merge(const agent_progress_render_result_t &lhs,
                                         const agent_progress_render_result_t &rhs) const;
    agent_progress_render_result_t apply_normalized_event(const agent_progress_event_t &event,
                                                          const provider_raw_event_t *raw_event);

    std::unique_ptr<i_provider_progress_adapter_t> adapter;
    agent_status_classifier_t classifier;
    agent_status_renderer_t renderer;
    bool verbose = false;
};

}  // namespace qcai2
