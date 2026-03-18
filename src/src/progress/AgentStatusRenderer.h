/*! Declares rendering helpers for interactive and stable-log agent status
 *  output modes.
 */
#pragma once

#include "AgentStatus.h"

#include <QString>

namespace qcai2
{

enum class agent_status_render_mode_t : std::uint8_t
{
    INTERACTIVE,
    NON_INTERACTIVE
};

struct agent_status_render_update_t
{
    bool status_changed = false;
    QString status_text;
    QString stable_log_line;
};

class agent_status_renderer_t
{
public:
    explicit agent_status_renderer_t(
        agent_status_render_mode_t mode = agent_status_render_mode_t::INTERACTIVE);

    agent_status_render_update_t render(const agent_status_snapshot_t &status);
    QString current_text() const;

private:
    agent_status_render_mode_t mode = agent_status_render_mode_t::INTERACTIVE;
    QString last_rendered_text;
};

}  // namespace qcai2
