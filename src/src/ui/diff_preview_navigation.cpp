/*! @file
    @brief Implements helpers that map Diff Preview lines to editor navigation targets.
*/

#include "diff_preview_navigation.h"

#include <QDir>
#include <QRegularExpression>

namespace qcai2
{

namespace
{

QString strip_diff_path_prefix(QString path)
{
    path = path.trimmed();
    const qsizetype tab_index = path.indexOf(QLatin1Char('\t'));
    if (tab_index >= 0)
    {
        path.truncate(tab_index);
    }
    if (path.startsWith(QStringLiteral("a/")) == true ||
        path.startsWith(QStringLiteral("b/")) == true)
    {
        path.remove(0, 2);
    }
    return path;
}

QString path_from_header_line(const QString &line, const QString &prefix)
{
    if (line.startsWith(prefix) == false)
    {
        return {};
    }

    const QString path = strip_diff_path_prefix(line.mid(prefix.size()));
    if (path == QStringLiteral("/dev/null"))
    {
        return {};
    }
    return path;
}

QString path_from_diff_git_line(const QString &line)
{
    static const QRegularExpression re(QStringLiteral(R"(^diff --git a/(.+) b/(.+)$)"));
    const QRegularExpressionMatch match = re.match(line);
    if (match.hasMatch() == false)
    {
        return {};
    }
    return strip_diff_path_prefix(match.captured(2));
}

QString path_from_index_line(const QString &line)
{
    if (line.startsWith(QStringLiteral("Index: ")) == false)
    {
        return {};
    }
    return strip_diff_path_prefix(line.mid(QStringLiteral("Index: ").size()));
}

QString absolute_path_for_relative(const QString &project_dir, const QString &relative_path)
{
    if (project_dir.trimmed().isEmpty() == true || relative_path.trimmed().isEmpty() == true)
    {
        return {};
    }
    return QDir::cleanPath(QDir(project_dir).absoluteFilePath(relative_path));
}

diff_preview_navigation_target_t build_target(const QString &absolute_file_path, int line)
{
    if (absolute_file_path.trimmed().isEmpty() == true)
    {
        return {};
    }

    diff_preview_navigation_target_t target;
    target.absolute_file_path = absolute_file_path;
    target.line = qMax(1, line);
    return target;
}

}  // namespace

bool diff_preview_navigation_target_t::is_valid() const
{
    return this->absolute_file_path.trimmed().isEmpty() == false && this->line > 0;
}

QHash<int, diff_preview_navigation_target_t>
build_diff_preview_navigation_targets(const QString &diff_text, const QString &project_dir)
{
    QHash<int, diff_preview_navigation_target_t> targets;
    if (diff_text.isEmpty() == true || project_dir.trimmed().isEmpty() == true)
    {
        return targets;
    }

    static const QRegularExpression re_hunk_header(
        QStringLiteral(R"(^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@)"));

    const QStringList lines = diff_text.split(QLatin1Char('\n'));
    QString current_old_path;
    QString current_new_path;
    QString current_target_path;
    int current_new_line = 1;

    auto active_target = [&]() {
        if (current_new_path.isEmpty() == false)
        {
            return build_target(current_new_path, current_new_line);
        }
        return build_target(current_old_path, current_new_line);
    };

    for (int index = 0; index < lines.size(); ++index)
    {
        const QString &line = lines[index];
        const int preview_line = index + 1;

        if (line.startsWith(QStringLiteral("diff --git ")) == true)
        {
            current_old_path.clear();
            current_new_path.clear();
            current_target_path =
                absolute_path_for_relative(project_dir, path_from_diff_git_line(line));
            current_new_line = 1;
            continue;
        }

        if (line.startsWith(QStringLiteral("Index: ")) == true)
        {
            current_target_path =
                absolute_path_for_relative(project_dir, path_from_index_line(line));
            continue;
        }

        if (line.startsWith(QStringLiteral("--- ")) == true)
        {
            current_old_path = absolute_path_for_relative(
                project_dir, path_from_header_line(line, QStringLiteral("--- ")));
            if (current_old_path.isEmpty() == false)
            {
                current_target_path = current_old_path;
            }
            continue;
        }

        if (line.startsWith(QStringLiteral("+++ ")) == true)
        {
            current_new_path = absolute_path_for_relative(
                project_dir, path_from_header_line(line, QStringLiteral("+++ ")));
            current_target_path =
                current_new_path.isEmpty() == false ? current_new_path : current_old_path;
            continue;
        }

        const QRegularExpressionMatch hunk_match = re_hunk_header.match(line);
        if (hunk_match.hasMatch() == true)
        {
            current_new_line = hunk_match.captured(2).toInt();
            continue;
        }

        if (current_target_path.isEmpty() == true)
        {
            continue;
        }

        if (line.startsWith(QLatin1Char('+')) == true &&
            line.startsWith(QStringLiteral("+++ ")) == false)
        {
            targets.insert(preview_line, active_target());
            ++current_new_line;
            continue;
        }

        if (line.startsWith(QLatin1Char('-')) == true &&
            line.startsWith(QStringLiteral("--- ")) == false)
        {
            targets.insert(preview_line, active_target());
            continue;
        }

        if (line.startsWith(QLatin1Char(' ')) == true)
        {
            ++current_new_line;
            continue;
        }
    }

    return targets;
}

}  // namespace qcai2
