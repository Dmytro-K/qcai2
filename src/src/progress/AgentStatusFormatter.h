/*! Declares formatting helpers for semantic agent status states. */
#pragma once

#include "AgentStatus.h"

#include <QString>

namespace qcai2
{

QString format_agent_status(const agent_status_snapshot_t &status);

}  // namespace qcai2
