/*! Implements # file reference popup completions and highlighting. */

#include "FileReferenceGoalHandler.h"

#include <QColor>

namespace qcai2
{

namespace
{

struct hash_token_state_t
{
    bool active = false;
    int start = 0;
    int end = 0;
};

bool is_token_boundary(const QString &text, int index)
{
    return index <= 0 || text.at(index - 1).isSpace();
}

hash_token_state_t hash_token_at_cursor(const QString &text, int cursor_position)
{
    for (int i = 0; i < text.size(); ++i)
    {
        if (text.at(i) != QLatin1Char('#') || !is_token_boundary(text, i))
        {
            continue;
        }

        int end = i + 1;
        while (end < text.size() && !text.at(end).isSpace())
        {
            ++end;
        }

        if (((cursor_position < i + 1 || cursor_position > end) == true))
        {
            continue;
        }

        return {true, i, end};
    }

    return {};
}

QList<hash_token_state_t> all_hash_tokens(const QString &text)
{
    QList<hash_token_state_t> tokens;
    for (int i = 0; i < text.size(); ++i)
    {
        if (text.at(i) != QLatin1Char('#') || !is_token_boundary(text, i))
        {
            continue;
        }

        int end = i + 1;
        while (end < text.size() && !text.at(end).isSpace())
        {
            ++end;
        }
        tokens.append({true, i, end});
        i = end - 1;
    }
    return tokens;
}

QColor file_reference_color(const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Highlight);
    return palette.color(QPalette::Base).lightness() < 128 ? base.lighter(130) : base.darker(120);
}

}  // namespace

file_reference_goal_handler_t::file_reference_goal_handler_t(
    candidate_provider_t candidate_provider)
    : candidate_provider(std::move(candidate_provider))
{
}

goal_completion_session_t
file_reference_goal_handler_t::completion_session(const goal_completion_request_t &request) const
{
    if (((!this->candidate_provider) == true))
    {
        return {};
    }

    const hash_token_state_t token = hash_token_at_cursor(request.text, request.cursor_position);
    if (token.active == false)
    {
        return {};
    }

    goal_completion_session_t session;
    session.active = true;
    session.replace_start = token.start;
    session.replace_end = token.end;
    session.prefix = request.text.mid(token.start, request.cursor_position - token.start);

    const QString file_prefix = session.prefix.mid(1);
    const QStringList candidates = this->candidate_provider();
    QList<goal_completion_item_t> case_sensitive_items;
    QList<goal_completion_item_t> case_insensitive_items;
    for (const QString &candidate : candidates)
    {
        if (!candidate.contains(file_prefix, Qt::CaseInsensitive))
        {
            continue;
        }

        goal_completion_item_t item{QStringLiteral("#%1").arg(candidate),
                                    QStringLiteral("#%1").arg(candidate), candidate};
        if (candidate.contains(file_prefix, Qt::CaseSensitive))
        {
            case_sensitive_items.append(item);
        }
        else
        {
            case_insensitive_items.append(item);
        }
    }

    session.items = case_sensitive_items;
    session.items.append(case_insensitive_items);

    return session;
}

QList<goal_highlight_span_t>
file_reference_goal_handler_t::highlight_spans(const QString &text, const QPalette &palette) const
{
    QList<goal_highlight_span_t> spans;
    QTextCharFormat format;
    format.setForeground(file_reference_color(palette));

    for (const hash_token_state_t &token : all_hash_tokens(text))
    {
        spans.append({token.start, token.end - token.start, format});
    }

    return spans;
}

}  // namespace qcai2
