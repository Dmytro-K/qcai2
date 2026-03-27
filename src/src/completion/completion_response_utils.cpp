/*! @file
    @brief Implements shared prompt-building and response-normalization helpers for AI completion.
*/

#include "completion_response_utils.h"
#include "completion_trigger_utils.h"

#include <QStringList>

namespace qcai2
{

namespace
{

QString trim_outer_blank_lines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    while (text.startsWith(QLatin1Char('\n')) == true)
    {
        text.remove(0, 1);
    }
    while (text.endsWith(QLatin1Char('\n')) == true)
    {
        text.chop(1);
    }
    return text;
}

QString strip_markdown_fences(QString text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("```")) == false)
    {
        return trim_outer_blank_lines(text);
    }

    qsizetype first_newline = trimmed.indexOf(QLatin1Char('\n'));
    if (first_newline < 0)
    {
        return {};
    }

    text = trimmed.mid(first_newline + 1);
    if (text.endsWith(QStringLiteral("```")) == true)
    {
        text.chop(3);
    }
    return trim_outer_blank_lines(text);
}

QString leading_whitespace(const QString &text)
{
    qsizetype count = 0;
    while (count < text.size() && text.at(count).isSpace() == true &&
           text.at(count) != QLatin1Char('\n'))
    {
        ++count;
    }
    return text.left(count);
}

int largest_suffix_prefix_overlap(const QString &left, const QString &right)
{
    const qsizetype max_overlap = qMin(left.size(), right.size());
    for (qsizetype overlap = max_overlap; overlap > 0; --overlap)
    {
        if (left.right(overlap) == right.left(overlap))
        {
            return static_cast<int>(overlap);
        }
    }
    return 0;
}

QString around_cursor_prefix_context(const QString &prefix)
{
    constexpr int k_max_context_chars = 1200;
    QString context = prefix.right(k_max_context_chars);
    if (context.size() < prefix.size())
    {
        const qsizetype first_newline = context.indexOf(QLatin1Char('\n'));
        if (first_newline >= 0)
        {
            context = context.mid(first_newline + 1);
        }
    }
    return context;
}

QString around_cursor_suffix_context(const QString &suffix)
{
    constexpr int k_max_context_chars = 400;
    QString context = suffix.left(k_max_context_chars);
    if (context.size() < suffix.size())
    {
        const qsizetype last_newline = context.lastIndexOf(QLatin1Char('\n'));
        if (last_newline > 0)
        {
            context = context.left(last_newline);
        }
    }
    return context;
}

QString trailing_identifier_fragment(const QString &line_prefix)
{
    const int fragment_length = identifier_fragment_length_before_cursor(
        line_prefix, static_cast<int>(line_prefix.size()));
    return fragment_length > 0 ? line_prefix.right(fragment_length) : QString();
}

bool is_control_flow_keyword(const QString &fragment)
{
    static const QStringList keywords = {
        QStringLiteral("if"),    QStringLiteral("else"),   QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("switch"), QStringLiteral("case"),
        QStringLiteral("do"),    QStringLiteral("catch"),  QStringLiteral("return")};
    return keywords.contains(fragment);
}

QChar first_non_space_character(const QString &text)
{
    for (const QChar character : text)
    {
        if (character.isSpace() == false)
        {
            return character;
        }
    }
    return {};
}

bool is_identifier_character(const QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

}  // namespace

QString current_line_prefix_before_cursor(const QString &prefix)
{
    const qsizetype last_newline = prefix.lastIndexOf(QLatin1Char('\n'));
    return last_newline >= 0 ? prefix.mid(last_newline + 1) : prefix;
}

QString current_line_suffix_after_cursor(const QString &suffix)
{
    const qsizetype next_newline = suffix.indexOf(QLatin1Char('\n'));
    return next_newline >= 0 ? suffix.left(next_newline) : suffix;
}

QString build_completion_prompt(const QString &file_name, const QString &prefix,
                                const QString &suffix)
{
    const QString line_prefix = current_line_prefix_before_cursor(prefix);
    const QString line_suffix = current_line_suffix_after_cursor(suffix);
    const QString before_context = around_cursor_prefix_context(prefix);
    const QString after_context = around_cursor_suffix_context(suffix);

    return QStringLiteral(
               "You are a code completion engine.\n"
               "Return ONLY the exact text to insert at <CURSOR>.\n"
               "Never repeat text that already exists before the cursor.\n"
               "Never repeat text that already exists after the cursor.\n"
               "Never rewrite the current line from the beginning.\n"
               "Prefer the shortest local continuation of the current token, statement, or "
               "block.\n"
               "If the user already typed part of a token or keyword, return only the remaining "
               "suffix or following punctuation/whitespace.\n"
               "File: %1\n\n"
               "Current line before cursor:\n"
               "~~~~text\n%2\n~~~~\n\n"
               "Current line after cursor:\n"
               "~~~~text\n%3\n~~~~\n\n"
               "Nearby code before cursor:\n"
               "~~~~text\n%4\n~~~~\n\n"
               "Nearby code after cursor:\n"
               "~~~~text\n%5\n~~~~\n\n"
               "Code with cursor:\n"
               "~~~~text\n%4<CURSOR>%5\n~~~~")
        .arg(file_name, line_prefix, line_suffix, before_context, after_context);
}

QString normalize_completion_response(const QString &raw_response, const QString &prefix,
                                      const QString &suffix)
{
    QString completion = strip_markdown_fences(raw_response);
    if (completion.trimmed().isEmpty() == true)
    {
        return {};
    }

    const QString line_prefix = current_line_prefix_before_cursor(prefix);
    const QString line_suffix = current_line_suffix_after_cursor(suffix);
    const QString line_indentation = leading_whitespace(line_prefix);
    const QString fragment = trailing_identifier_fragment(line_prefix);

    const int prefix_overlap = largest_suffix_prefix_overlap(line_prefix, completion);
    if (prefix_overlap > 0)
    {
        completion.remove(0, prefix_overlap);
    }

    const int suffix_overlap = largest_suffix_prefix_overlap(completion, line_suffix);
    if (suffix_overlap > 0)
    {
        completion.chop(suffix_overlap);
    }

    const QString trimmed_completion = completion.trimmed();
    if (trimmed_completion.isEmpty() == true)
    {
        return {};
    }

    if (line_prefix.trimmed().isEmpty() == false && prefix_overlap == 0)
    {
        if (line_indentation.isEmpty() == false && completion.startsWith(line_indentation) == true)
        {
            return {};
        }

        if (trimmed_completion.startsWith(QStringLiteral("//")) == true ||
            trimmed_completion.startsWith(QStringLiteral("/*")) == true)
        {
            return {};
        }

        if (is_control_flow_keyword(fragment) == true)
        {
            const QChar first_character = first_non_space_character(completion);
            if (is_identifier_character(first_character) == true)
            {
                return {};
            }
        }
    }

    return completion;
}

QString build_inline_completion_text(const QString &prefix, const QString &completion,
                                     const QString &suffix)
{
    return current_line_prefix_before_cursor(prefix) + completion +
           current_line_suffix_after_cursor(suffix);
}

QString stable_streaming_completion_prefix(const QString &completion)
{
    if (completion.isEmpty() == true)
    {
        return {};
    }

    const QChar last_character = completion.back();
    if (is_identifier_character(last_character) == false)
    {
        return completion;
    }

    int stable_length = static_cast<int>(completion.size());
    while (stable_length > 0 && is_identifier_character(completion.at(stable_length - 1)) == true)
    {
        --stable_length;
    }

    return stable_length > 0 ? completion.left(stable_length) : QString();
}

}  // namespace qcai2
