/*! Declares semantic agent status phases and the classifier that maps normalized
 *  progress events into user-facing status states.
 */
#pragma once

#include "AgentProgress.h"

#include <QString>

#include <cstdint>

namespace qcai2
{

enum class AgentStatusKind : std::uint8_t
{
    Idle,
    Thinking,
    Exploring,
    Searching,
    Reading,
    RunningTool,
    Validating,
    Testing,
    Building,
    ApplyingChanges,
    PreparingFinalAnswer,
    Done,
    Error
};

struct AgentStatusSnapshot
{
    AgentStatusKind kind = AgentStatusKind::Idle;
    QString subject;
    QString message;
};

QString agentStatusKindName(AgentStatusKind kind);

class AgentStatusClassifier
{
public:
    bool apply(const AgentProgressEvent &event, AgentStatusSnapshot *changedStatus = nullptr);
    AgentStatusSnapshot currentStatus() const;

private:
    bool setStatus(const AgentStatusSnapshot &status, AgentStatusSnapshot *changedStatus);
    AgentStatusSnapshot toolStatus(const AgentProgressEvent &event) const;

    AgentStatusSnapshot m_current;
};

}  // namespace qcai2
