/*! Declares goal-editor support for # file references. */
#pragma once

#include "GoalSpecialHandler.h"

#include <functional>

namespace qcai2
{

/**
 * Special-token handler that powers # file completion and highlighting.
 */
class FileReferenceGoalHandler final : public GoalSpecialHandler
{
public:
    using CandidateProvider = std::function<QStringList()>;

    explicit FileReferenceGoalHandler(CandidateProvider candidateProvider);

    GoalCompletionSession completionSession(const GoalCompletionRequest &request) const override;
    QList<GoalHighlightSpan> highlightSpans(const QString &text,
                                            const QPalette &palette) const override;

private:
    CandidateProvider m_candidateProvider;
};

}  // namespace qcai2
