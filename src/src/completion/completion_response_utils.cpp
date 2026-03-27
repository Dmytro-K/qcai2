/*! @file
    @brief Implements shared prompt-building and response-normalization helpers for AI completion.
*/

#include "completion_response_utils.h"
#include "completion_trigger_utils.h"

#include "qompi/completion_policy.h"

#include <QStringList>

namespace qcai2
{

namespace
{

std::string to_std_string(const QString &value)
{
    return value.toUtf8().toStdString();
}

QString from_std_string(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
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

}  // namespace

QString current_line_prefix_before_cursor(const QString &prefix)
{
    return from_std_string(qompi::current_line_prefix_before_cursor(to_std_string(prefix)));
}

QString current_line_suffix_after_cursor(const QString &suffix)
{
    return from_std_string(qompi::current_line_suffix_after_cursor(to_std_string(suffix)));
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
               "Return ONLY the exact text to insert at <fim_middle>.\n"
               "Do not repeat any text that already exists in <fim_prefix>.\n"
               "Do not repeat any text that already exists in <fim_suffix>.\n"
               "Do not wrap the answer in markdown fences or explanations.\n"
               "If the cursor is in the middle of a line, continue exactly from that point.\n"
               "Preserve valid multiline code when it belongs after the cursor.\n"
               "File: %1\n\n"
               "Current line before cursor:\n"
               "~~~~text\n%2\n~~~~\n\n"
               "Current line after cursor:\n"
               "~~~~text\n%3\n~~~~\n\n"
               "Complete the code between these markers:\n"
               "<fim_prefix>\n"
               "%4\n"
               "<fim_suffix>\n"
               "%5\n"
               "<fim_middle>")
        .arg(file_name, line_prefix, line_suffix, before_context, after_context);
}

QString normalize_completion_response(const QString &raw_response, const QString &prefix,
                                      const QString &suffix)
{
    return from_std_string(qompi::normalize_completion_response(
        to_std_string(prefix), to_std_string(suffix), to_std_string(raw_response)));
}

QString build_inline_completion_text(const QString &prefix, const QString &completion,
                                     const QString &suffix)
{
    return from_std_string(qompi::build_inline_completion_text(
        to_std_string(prefix), to_std_string(suffix), to_std_string(completion)));
}

completion_text_extent_t measure_completion_text_extent(const QString &text,
                                                        const int start_column)
{
    completion_text_extent_t extent;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (lines.isEmpty() == true)
    {
        extent.end_column = start_column;
        return extent;
    }
    extent.end_line_offset = static_cast<int>(lines.size() - 1);
    extent.end_column = extent.end_line_offset == 0
                            ? start_column + static_cast<int>(lines.constFirst().size())
                            : static_cast<int>(lines.constLast().size());
    return extent;
}

QString stable_streaming_completion_prefix(const QString &completion)
{
    return from_std_string(qompi::stable_streaming_completion_prefix(to_std_string(completion)));
}

}  // namespace qcai2
