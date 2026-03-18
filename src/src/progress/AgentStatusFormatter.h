/*! Declares formatting helpers for semantic agent status states. */
#pragma once

#include "AgentStatus.h"

#include <QString>

namespace qcai2
{

QString formatAgentStatus(const AgentStatusSnapshot &status);

}  // namespace qcai2
