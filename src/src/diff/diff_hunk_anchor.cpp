/*! @file
    @brief Implements anchor-line calculation for unified diff hunks.
*/

#include "diff_hunk_anchor.h"

#include <QStringList>
#include <QtGlobal>

namespace qcai2
{

namespace
{

bool is_added_hunk_line(const QString &line)
{
    return line.startsWith(QLatin1Char('+')) == true &&
           line.startsWith(QStringLiteral("+++ ")) == false;
}

bool is_removed_hunk_line(const QString &line)
{
    return line.startsWith(QLatin1Char('-')) == true &&
           line.startsWith(QStringLiteral("--- ")) == false;
}

bool is_context_hunk_line(const QString &line)
{
    return line.startsWith(QLatin1Char(' ')) == true;
}

}  // namespace

int first_change_old_line_for_hunk(int start_line_old, int start_line_new,
                                   const QString &hunk_body)
{
    int current_old_line = start_line_old;
    int current_new_line = start_line_new;

    const QStringList lines = hunk_body.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        if (line.startsWith(QStringLiteral("\\ No newline at end of file")) == true)
        {
            continue;
        }

        if (is_context_hunk_line(line) == true)
        {
            ++current_old_line;
            ++current_new_line;
            continue;
        }

        if (is_removed_hunk_line(line) == true)
        {
            return qMax(1, current_old_line);
        }

        if (is_added_hunk_line(line) == true)
        {
            if (current_old_line > 0)
            {
                return current_old_line;
            }
            return qMax(1, current_new_line);
        }
    }

    if (start_line_old > 0)
    {
        return qMax(1, start_line_old);
    }
    return qMax(1, start_line_new);
}

}  // namespace qcai2
