/*! @file
    @brief Declares shared prompt-building and response-normalization helpers for AI completion.
*/

#pragma once

#include <QString>

namespace qcai2
{

/**
 * Describes the rendered extent of one completion text relative to its starting column.
 */
struct completion_text_extent_t
{
    /** Number of extra lines added by the text. */
    int end_line_offset = 0;

    /** Column of the final cursor position after the text is laid out. */
    int end_column = 0;
};

/**
 * Returns the current-line text before the cursor.
 * @param prefix Full prompt prefix captured before the cursor.
 * @return Text from the last newline up to the cursor.
 */
QString current_line_prefix_before_cursor(const QString &prefix);

/**
 * Returns the current-line text after the cursor.
 * @param suffix Full prompt suffix captured after the cursor.
 * @return Text from the cursor up to the next newline.
 */
QString current_line_suffix_after_cursor(const QString &suffix);

/**
 * Builds one fill-in-the-middle completion prompt that asks for insertion-only output.
 * @param file_name Source file being completed.
 * @param prefix Full prompt prefix captured before the cursor.
 * @param suffix Full prompt suffix captured after the cursor.
 * @return Prompt text sent to the provider.
 */
QString build_completion_prompt(const QString &file_name, const QString &prefix,
                                const QString &suffix);

/**
 * Normalizes one provider response into the exact text that should be inserted at the cursor.
 * @param raw_response Raw provider response text.
 * @param prefix Full prompt prefix captured before the cursor.
 * @param suffix Full prompt suffix captured after the cursor.
 * @return Insertable completion text, or empty when the response looks incompatible.
 */
QString normalize_completion_response(const QString &raw_response, const QString &prefix,
                                      const QString &suffix);

/**
 * Builds one full current-line replacement string for Qt Creator inline suggestions.
 * @param prefix Full prompt prefix captured before the cursor.
 * @param completion Normalized insertion-only completion text.
 * @param suffix Full prompt suffix captured after the cursor.
 * @return Current line before cursor + completion + current line after cursor.
 */
QString build_inline_completion_text(const QString &prefix, const QString &completion,
                                     const QString &suffix);

/**
 * Measures the final line/column extent of one rendered completion text.
 * @param text Full suggestion text passed to the inline suggestion API.
 * @param start_column Column where the text starts on the first line.
 * @return Final line offset and end column after rendering the text.
 */
completion_text_extent_t measure_completion_text_extent(const QString &text, int start_column);

/**
 * Returns the stable prefix of one streaming completion suitable for partial inline display.
 * @param completion Normalized insertion-only completion accumulated so far.
 * @return Longest prefix that does not end mid-identifier.
 */
QString stable_streaming_completion_prefix(const QString &completion);

}  // namespace qcai2
