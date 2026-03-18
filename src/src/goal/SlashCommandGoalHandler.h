/*! Declares goal-editor support for slash-command completions and coloring. */
#pragma once

#include "GoalSpecialHandler.h"

namespace qcai2
{

class slash_command_registry_t;

/**
 * Special-token handler that powers slash-command popup completions and styling.
 */
class slash_command_goal_handler_t final : public goal_special_handler_t
{
public:
    explicit slash_command_goal_handler_t(const slash_command_registry_t *registry);

    goal_completion_session_t
    completion_session(const goal_completion_request_t &request) const override;
    QList<goal_highlight_span_t> highlight_spans(const QString &text,
                                                 const QPalette &palette) const override;

private:
    const slash_command_registry_t *registry = nullptr;
};

}  // namespace qcai2
