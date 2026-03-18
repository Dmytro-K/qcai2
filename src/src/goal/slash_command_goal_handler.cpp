/*! Implements slash-command popup completions and highlighting for the goal editor. */

#include "slash_command_goal_handler.h"

#include "../commands/slash_command_registry.h"

namespace qcai2
{

namespace
{

struct slash_token_state_t
{
    bool has_leading_slash = false;
    int start = 0;
    int end = 0;
};

slash_token_state_t leading_slash_token_state(const QString &text)
{
    int first_non_space = 0;
    while (first_non_space < text.size() && text.at(first_non_space).isSpace())
    {
        ++first_non_space;
    }

    if (first_non_space >= text.size() || text.at(first_non_space) != QLatin1Char('/'))
    {
        return {};
    }

    int end = first_non_space + 1;
    while (end < text.size() && !text.at(end).isSpace())
    {
        ++end;
    }

    slash_token_state_t state;
    state.has_leading_slash = true;
    state.start = first_non_space;
    state.end = end;
    return state;
}

}  // namespace

slash_command_goal_handler_t::slash_command_goal_handler_t(
    const slash_command_registry_t *registry)
    : registry(registry)
{
}

goal_completion_session_t
slash_command_goal_handler_t::completion_session(const goal_completion_request_t &request) const
{
    if (((this->registry == nullptr) == true))
    {
        return {};
    }

    const slash_token_state_t state = leading_slash_token_state(request.text);
    if (((!state.has_leading_slash || request.cursor_position < state.start + 1 ||
          request.cursor_position > state.end) == true))
    {
        return {};
    }

    goal_completion_session_t session;
    session.active = true;
    session.replace_start = state.start;
    session.replace_end = state.end;
    session.prefix = request.text.mid(state.start, request.cursor_position - state.start);

    for (const slash_command_registry_t::command_info_t &command : this->registry->commands())
    {
        if (!command.name.startsWith(session.prefix, Qt::CaseInsensitive))
        {
            continue;
        }

        session.items.append({command.name, command.name, command.description});
    }

    return session;
}

QList<goal_highlight_span_t>
slash_command_goal_handler_t::highlight_spans(const QString &text, const QPalette &palette) const
{
    const slash_token_state_t state = leading_slash_token_state(text);
    if (state.has_leading_slash == false)
    {
        return {};
    }

    QTextCharFormat format;
    format.setForeground(palette.color(QPalette::Link));

    return {{state.start, state.end - state.start, format}};
}

}  // namespace qcai2
