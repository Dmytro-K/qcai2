/*! Declares rendering helpers for interactive and stable-log agent status
 *  output modes.
 */
#pragma once

#include "AgentStatus.h"

#include <QString>

namespace qcai2
{

enum class AgentStatusRenderMode : std::uint8_t
{
    Interactive,
    NonInteractive
};

struct AgentStatusRenderUpdate
{
    bool statusChanged = false;
    QString statusText;
    QString stableLogLine;
};

class AgentStatusRenderer
{
public:
    explicit AgentStatusRenderer(AgentStatusRenderMode mode = AgentStatusRenderMode::Interactive);

    AgentStatusRenderUpdate render(const AgentStatusSnapshot &status);
    QString currentText() const;

private:
    AgentStatusRenderMode m_mode = AgentStatusRenderMode::Interactive;
    QString m_lastRenderedText;
};

}  // namespace qcai2
