/*! Declares goal-editor support for # file references. */
#pragma once

#include "GoalSpecialHandler.h"

#include <functional>

namespace qcai2
{

/**
 * Special-token handler that powers # file completion and highlighting.
 */
class file_reference_goal_handler_t final : public goal_special_handler_t
{
public:
    using candidate_provider_t = std::function<QStringList()>;

    explicit file_reference_goal_handler_t(candidate_provider_t candidate_provider);

    goal_completion_session_t
    completion_session(const goal_completion_request_t &request) const override;
    QList<goal_highlight_span_t> highlight_spans(const QString &text,
                                                 const QPalette &palette) const override;

private:
    candidate_provider_t candidate_provider;
};

}  // namespace qcai2
