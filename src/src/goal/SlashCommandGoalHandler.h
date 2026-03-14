/*! Declares goal-editor support for slash-command completions and coloring. */
#pragma once

#include "GoalSpecialHandler.h"

namespace qcai2
{

class SlashCommandRegistry;

/**
 * Special-token handler that powers slash-command popup completions and styling.
 */
class SlashCommandGoalHandler final : public GoalSpecialHandler
{
public:
    explicit SlashCommandGoalHandler(const SlashCommandRegistry *registry);

    GoalCompletionSession completionSession(const GoalCompletionRequest &request) const override;
    QList<GoalHighlightSpan> highlightSpans(const QString &text,
                                            const QPalette &palette) const override;

private:
    const SlashCommandRegistry *m_registry = nullptr;
};

}  // namespace qcai2
