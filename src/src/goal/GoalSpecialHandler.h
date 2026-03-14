/*! Declares extensible special-token handling for the goal editor. */
#pragma once

#include <QList>
#include <QPalette>
#include <QString>
#include <QTextCharFormat>

namespace qcai2
{

/**
 * One popup completion entry contributed by a special-token handler.
 */
struct GoalCompletionItem
{
    /** Main label shown in the popup. */
    QString label;

    /** Text inserted into the editor when the item is accepted. */
    QString insertText;

    /** Optional secondary detail text for future popup renderers. */
    QString detail;
};

/**
 * Completion session returned by a special-token handler for the current caret position.
 */
struct GoalCompletionSession
{
    /** True when the handler is active at the current caret position. */
    bool active = false;

    /** Document offset where replacement should begin. */
    int replaceStart = 0;

    /** Document offset where replacement should end. */
    int replaceEnd = 0;

    /** Prefix already typed by the user. */
    QString prefix;

    /** Completion items to show in the popup. */
    QList<GoalCompletionItem> items;
};

/**
 * Goal-editor context passed to special-token completion handlers.
 */
struct GoalCompletionRequest
{
    /** Entire plain-text editor contents. */
    QString text;

    /** Caret position inside the document. */
    int cursorPosition = 0;
};

/**
 * One colored span contributed by a special-token handler.
 */
struct GoalHighlightSpan
{
    /** Document offset where the colored span begins. */
    int start = 0;

    /** Character count covered by the span. */
    int length = 0;

    /** Text format applied to the span. */
    QTextCharFormat format;
};

/**
 * Shared extension point for goal-editor popup completions and colored spans.
 */
class GoalSpecialHandler
{
public:
    virtual ~GoalSpecialHandler() = default;

    /**
     * Returns popup completion data for the current caret position.
     */
    virtual GoalCompletionSession completionSession(
        const GoalCompletionRequest &request) const = 0;

    /**
     * Returns colored text spans for the current goal contents.
     */
    virtual QList<GoalHighlightSpan> highlightSpans(const QString &text,
                                                    const QPalette &palette) const = 0;
};

}  // namespace qcai2
