/*! Declares semantic agent status phases and the classifier that maps normalized
 *  progress events into user-facing status states.
 */
#pragma once

#include "AgentProgress.h"

#include <QString>

#include <cstdint>

namespace qcai2
{

enum class agent_status_kind_t : std::uint8_t
{
    IDLE,
    THINKING,
    EXPLORING,
    SEARCHING,
    READING,
    RUNNING_TOOL,
    VALIDATING,
    TESTING,
    BUILDING,
    APPLYING_CHANGES,
    PREPARING_FINAL_ANSWER,
    DONE,
    ERROR
};

struct agent_status_snapshot_t
{
    agent_status_kind_t kind = agent_status_kind_t::IDLE;
    QString subject;
    QString message;
};

QString agent_status_kind_name(agent_status_kind_t kind);

class agent_status_classifier_t
{
public:
    bool apply(const agent_progress_event_t &event,
               agent_status_snapshot_t *changed_status = nullptr);
    agent_status_snapshot_t current_status() const;

private:
    bool set_status(const agent_status_snapshot_t &status,
                    agent_status_snapshot_t *changed_status);
    agent_status_snapshot_t tool_status(const agent_progress_event_t &event) const;

    agent_status_snapshot_t current;
};

}  // namespace qcai2
