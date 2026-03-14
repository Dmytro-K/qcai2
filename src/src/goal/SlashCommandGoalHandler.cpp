/*! Implements slash-command popup completions and highlighting for the goal editor. */

#include "SlashCommandGoalHandler.h"

#include "../commands/SlashCommandRegistry.h"

namespace qcai2
{

namespace
{

struct SlashTokenState
{
    bool hasLeadingSlash = false;
    int start = 0;
    int end = 0;
};

SlashTokenState leadingSlashTokenState(const QString &text)
{
    int firstNonSpace = 0;
    while (firstNonSpace < text.size() && text.at(firstNonSpace).isSpace())
        ++firstNonSpace;

    if (firstNonSpace >= text.size() || text.at(firstNonSpace) != QLatin1Char('/'))
        return {};

    int end = firstNonSpace + 1;
    while (end < text.size() && !text.at(end).isSpace())
        ++end;

    SlashTokenState state;
    state.hasLeadingSlash = true;
    state.start = firstNonSpace;
    state.end = end;
    return state;
}

}  // namespace

SlashCommandGoalHandler::SlashCommandGoalHandler(const SlashCommandRegistry *registry)
    : m_registry(registry)
{
}

GoalCompletionSession SlashCommandGoalHandler::completionSession(
    const GoalCompletionRequest &request) const
{
    if (m_registry == nullptr)
        return {};

    const SlashTokenState state = leadingSlashTokenState(request.text);
    if (!state.hasLeadingSlash || request.cursorPosition < state.start + 1 ||
        request.cursorPosition > state.end)
    {
        return {};
    }

    GoalCompletionSession session;
    session.active = true;
    session.replaceStart = state.start;
    session.replaceEnd = state.end;
    session.prefix = request.text.mid(state.start, request.cursorPosition - state.start);

    for (const SlashCommandRegistry::CommandInfo &command : m_registry->commands())
    {
        if (!command.name.startsWith(session.prefix, Qt::CaseInsensitive))
            continue;

        session.items.append({command.name, command.name, command.description});
    }

    return session;
}

QList<GoalHighlightSpan> SlashCommandGoalHandler::highlightSpans(const QString &text,
                                                                 const QPalette &palette) const
{
    const SlashTokenState state = leadingSlashTokenState(text);
    if (!state.hasLeadingSlash)
        return {};

    QTextCharFormat format;
    format.setForeground(palette.color(QPalette::Link));

    return {{state.start, state.end - state.start, format}};
}

}  // namespace qcai2
