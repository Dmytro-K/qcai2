/*! Implements text-range conversion for one parsed unified-diff hunk. */
#include "diff_hunk_text_edit.h"

#include <QVector>

namespace qcai2
{

namespace
{

enum class parsed_hunk_line_kind_t
{
    none,
    context,
    removed,
    added,
};

QVector<int> line_start_positions(const QString &document_text)
{
    QVector<int> starts;
    starts.reserve(document_text.count(QLatin1Char('\n')) + 1);
    starts.append(0);
    for (int index = 0; index < document_text.size(); ++index)
    {
        if (document_text.at(index) == QLatin1Char('\n'))
        {
            starts.append(index + 1);
        }
    }
    return starts;
}

int line_start_position_for_one_based_line(const QVector<int> &starts, int one_based_line,
                                           qsizetype document_size)
{
    if (one_based_line <= 1)
    {
        return 0;
    }

    const int index = one_based_line - 1;
    if (index >= 0 && index < starts.size())
    {
        return starts.at(index);
    }

    return int(document_size);
}

QString build_hunk_side_text(const QStringList &lines, bool has_trailing_newline)
{
    QString result = lines.join(QLatin1Char('\n'));
    if (has_trailing_newline == true && lines.isEmpty() == false)
    {
        result.append(QLatin1Char('\n'));
    }
    return result;
}

}  // namespace

std::optional<hunk_text_edit_t> build_hunk_text_edit(const diff_hunk_t &hunk,
                                                     int prior_accepted_line_delta,
                                                     const QString &document_text,
                                                     QString *error_message)
{
    QStringList old_lines;
    QStringList new_lines;
    bool old_has_trailing_newline = true;
    bool new_has_trailing_newline = true;
    parsed_hunk_line_kind_t last_line_kind = parsed_hunk_line_kind_t::none;

    const QStringList body_lines = hunk.hunk_body.split(QLatin1Char('\n'));
    for (const QString &line : body_lines)
    {
        if (line.startsWith(QStringLiteral("\\ No newline at end of file")))
        {
            switch (last_line_kind)
            {
                case parsed_hunk_line_kind_t::context:
                    old_has_trailing_newline = false;
                    new_has_trailing_newline = false;
                    break;
                case parsed_hunk_line_kind_t::removed:
                    old_has_trailing_newline = false;
                    break;
                case parsed_hunk_line_kind_t::added:
                    new_has_trailing_newline = false;
                    break;
                case parsed_hunk_line_kind_t::none:
                    break;
            }
            continue;
        }

        if (line.isEmpty() == true)
        {
            if (error_message != nullptr)
            {
                *error_message = QStringLiteral("Encountered malformed empty hunk body line");
            }
            return std::nullopt;
        }

        const QChar prefix = line.at(0);
        const QString payload = line.mid(1);
        switch (prefix.toLatin1())
        {
            case ' ':
                old_lines.append(payload);
                new_lines.append(payload);
                last_line_kind = parsed_hunk_line_kind_t::context;
                break;
            case '-':
                old_lines.append(payload);
                last_line_kind = parsed_hunk_line_kind_t::removed;
                break;
            case '+':
                new_lines.append(payload);
                last_line_kind = parsed_hunk_line_kind_t::added;
                break;
            default:
                if (error_message != nullptr)
                {
                    *error_message =
                        QStringLiteral("Encountered unsupported hunk line prefix '%1'")
                            .arg(prefix);
                }
                return std::nullopt;
        }
    }

    if (old_lines.size() != hunk.count_old || new_lines.size() != hunk.count_new)
    {
        if (error_message != nullptr)
        {
            *error_message =
                QStringLiteral("Hunk line counts do not match header old=%1/%2 new=%3/%4")
                    .arg(old_lines.size())
                    .arg(hunk.count_old)
                    .arg(new_lines.size())
                    .arg(hunk.count_new);
        }
        return std::nullopt;
    }

    const QVector<int> starts = line_start_positions(document_text);
    const int document_size = int(document_text.size());
    const int adjusted_old_start = hunk.start_line_old + prior_accepted_line_delta;

    int start_position = 0;
    int end_position = 0;
    if (hunk.count_old == 0)
    {
        start_position = adjusted_old_start <= 0
                             ? 0
                             : line_start_position_for_one_based_line(
                                   starts, adjusted_old_start + 1, document_size);
        end_position = start_position;
    }
    else
    {
        start_position =
            line_start_position_for_one_based_line(starts, adjusted_old_start, document_size);
        end_position = line_start_position_for_one_based_line(
            starts, adjusted_old_start + hunk.count_old, document_size);
    }

    if (start_position < 0 || end_position < start_position || end_position > document_size)
    {
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("Computed invalid document range [%1, %2)")
                                 .arg(start_position)
                                 .arg(end_position);
        }
        return std::nullopt;
    }

    hunk_text_edit_t edit;
    edit.start_position = start_position;
    edit.end_position = end_position;
    edit.expected_text = build_hunk_side_text(old_lines, old_has_trailing_newline);
    edit.replacement_text = build_hunk_side_text(new_lines, new_has_trailing_newline);

    const QString current_range_text =
        document_text.mid(edit.start_position, edit.end_position - edit.start_position);
    if (current_range_text != edit.expected_text)
    {
        if (error_message != nullptr)
        {
            *error_message = QStringLiteral("Document range mismatch for hunk at line %1")
                                 .arg(adjusted_old_start);
        }
        return std::nullopt;
    }

    return edit;
}

}  // namespace qcai2
