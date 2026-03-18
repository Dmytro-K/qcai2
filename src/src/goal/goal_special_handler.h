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
struct goal_completion_item_t
{
    /** Main label shown in the popup. */
    QString label;

    /** Text inserted into the editor when the item is accepted. */
    QString insert_text;

    /** Optional secondary detail text for future popup renderers. */
    QString detail;
};

/**
 * Completion session returned by a special-token handler for the current caret position.
 */
struct goal_completion_session_t
{
    /** True when the handler is active at the current caret position. */
    bool active = false;

    /** Document offset where replacement should begin. */
    int replace_start = 0;

    /** Document offset where replacement should end. */
    int replace_end = 0;

    /** Prefix already typed by the user. */
    QString prefix;

    /** Completion items to show in the popup. */
    QList<goal_completion_item_t> items;
};

/**
 * Goal-editor context passed to special-token completion handlers.
 */
struct goal_completion_request_t
{
    /** Entire plain-text editor contents. */
    QString text;

    /** Caret position inside the document. */
    int cursor_position = 0;
};

/**
 * One colored span contributed by a special-token handler.
 */
struct goal_highlight_span_t
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
class goal_special_handler_t
{
public:
    virtual ~goal_special_handler_t() = default;

    /**
     * Returns popup completion data for the current caret position.
     */
    virtual goal_completion_session_t
    completion_session(const goal_completion_request_t &request) const = 0;

    /**
     * Returns colored text spans for the current goal contents.
     */
    virtual QList<goal_highlight_span_t> highlight_spans(const QString &text,
                                                         const QPalette &palette) const = 0;
};

}  // namespace qcai2
