/*! @file
    @brief Implements shared helper functions for autocomplete trigger decisions.
*/

#include "completion_trigger_utils.h"

namespace qcai2
{

namespace
{

bool is_identifier_character(const QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

bool is_traditional_completion_activation_char(const QChar character)
{
    return character == QLatin1Char('.') || character == QLatin1Char('>') ||
           character == QLatin1Char(':') || character == QLatin1Char('(');
}

}  // namespace

int identifier_fragment_length_before_cursor(const QString &line_text, const int cursor_column)
{
    if (cursor_column <= 0)
    {
        return 0;
    }

    int fragment_length = 0;
    for (int index = qMin(cursor_column, static_cast<int>(line_text.size())) - 1; index >= 0;
         --index)
    {
        if (is_identifier_character(line_text.at(index)) == false)
        {
            break;
        }
        ++fragment_length;
    }
    return fragment_length;
}

bool is_non_whitespace_typed_insertion(const QString &inserted_text)
{
    return inserted_text.trimmed().isEmpty() == false;
}

int typed_fragment_end_offset(const QString &inserted_text)
{
    for (qsizetype index = inserted_text.size(); index > 0; --index)
    {
        if (inserted_text.at(index - 1).isSpace() == false)
        {
            return static_cast<int>(index);
        }
    }

    return 0;
}

int typed_fragment_trigger_column(const QString &inserted_text,
                                  const int trigger_column_after_insert)
{
    const int fragment_end_offset = typed_fragment_end_offset(inserted_text);
    if (fragment_end_offset <= 0)
    {
        return trigger_column_after_insert;
    }

    const int trailing_whitespace_chars =
        static_cast<int>(inserted_text.size()) - fragment_end_offset;
    return qMax(0, trigger_column_after_insert - trailing_whitespace_chars);
}

bool should_auto_trigger_completion(const QString &line_text, const int cursor_column,
                                    const int completion_min_chars)
{
    if (cursor_column <= 0 || cursor_column > line_text.size())
    {
        return false;
    }

    const QChar last_character = line_text.at(cursor_column - 1);
    if (is_traditional_completion_activation_char(last_character) == true)
    {
        return true;
    }

    if (is_identifier_character(last_character) == false || completion_min_chars <= 0)
    {
        return false;
    }

    return identifier_fragment_length_before_cursor(line_text, cursor_column) >=
           completion_min_chars;
}

}  // namespace qcai2
