/*! @file
    @brief Implements helpers for clickable file-location links in the Actions Log.
*/

#include "actions_log_links.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrlQuery>

namespace qcai2
{

namespace
{

constexpr auto actions_log_file_scheme = "qcai2-open-file";

Qt::CaseSensitivity canonical_path_case_sensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalize_canonical_path_for_compare(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool canonical_path_is_inside_root(const QString &canonical_path, const QString &root_path)
{
    const QString normalized_root = normalize_canonical_path_for_compare(root_path);
    const QString normalized_path = normalize_canonical_path_for_compare(canonical_path);
    if (normalized_root.isEmpty() == true || normalized_path.isEmpty() == true)
    {
        return false;
    }

    const Qt::CaseSensitivity case_sensitivity = canonical_path_case_sensitivity();
    if (normalized_path.compare(normalized_root, case_sensitivity) == 0)
    {
        return true;
    }

    return normalized_path.startsWith(normalized_root + QLatin1Char('/'), case_sensitivity);
}

QRegularExpression file_reference_re()
{
    return QRegularExpression(
        QStringLiteral(
            R"((?<![\w./-])((?:(?:[A-Za-z]:)?[\\/]|\.\.?[\\/])?[A-Za-z0-9_+.\-~]+(?:[\\/][A-Za-z0-9_+.\-~]+)+):([1-9]\d*)(?::([1-9]\d*))?)"),
        QRegularExpression::UseUnicodePropertiesOption);
}

QRegularExpression markdown_token_re()
{
    return QRegularExpression(QStringLiteral(R"(\[[^\]]+\]\([^)]+\)|`[^`\n]+`)"));
}

bool is_fence_delimiter_line(const QString &line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~~"));
}

QString make_internal_link(const QString &display_text, const QString &path, int line, int column)
{
    QUrl url;
    url.setScheme(QLatin1StringView(actions_log_file_scheme));
    url.setPath(QStringLiteral("/open"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), path);
    query.addQueryItem(QStringLiteral("line"), QString::number(line));
    if (column > 0)
    {
        query.addQueryItem(QStringLiteral("column"), QString::number(column));
    }
    url.setQuery(query);

    return QStringLiteral("[%1](%2)").arg(display_text, url.toString(QUrl::FullyEncoded));
}

QString linkify_plain_segment(const QString &text)
{
    QString rewritten;
    qsizetype cursor = 0;
    const QRegularExpression pattern = file_reference_re();
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext() == true)
    {
        const QRegularExpressionMatch match = it.next();
        rewritten += text.mid(cursor, match.capturedStart() - cursor);
        const QString display_text = match.captured(0);
        const int line = match.captured(2).toInt();
        const int column = match.captured(3).isEmpty() == true ? 0 : match.captured(3).toInt();
        rewritten += make_internal_link(display_text, match.captured(1), line, column);
        cursor = match.capturedEnd();
    }
    rewritten += text.mid(cursor);
    return rewritten;
}

QString linkify_non_fenced_line(const QString &line)
{
    QString rewritten;
    qsizetype cursor = 0;
    const QRegularExpression token_pattern = markdown_token_re();
    QRegularExpressionMatchIterator it = token_pattern.globalMatch(line);
    while (it.hasNext() == true)
    {
        const QRegularExpressionMatch match = it.next();
        rewritten += linkify_plain_segment(line.mid(cursor, match.capturedStart() - cursor));

        const QString token = match.captured(0);
        if (token.startsWith(QLatin1Char('`')) && token.endsWith(QLatin1Char('`')))
        {
            const QString inline_code = token.mid(1, token.size() - 2);
            const QRegularExpressionMatch file_match = file_reference_re().match(inline_code);
            if (file_match.hasMatch() == true &&
                file_match.capturedLength(0) == inline_code.size())
            {
                const int line_number = file_match.captured(2).toInt();
                const int column =
                    file_match.captured(3).isEmpty() == true ? 0 : file_match.captured(3).toInt();
                rewritten +=
                    make_internal_link(inline_code, file_match.captured(1), line_number, column);
            }
            else
            {
                rewritten += token;
            }
        }
        else
        {
            rewritten += token;
        }
        cursor = match.capturedEnd();
    }

    rewritten += linkify_plain_segment(line.mid(cursor));
    return rewritten;
}

}  // namespace

QString linkify_actions_log_file_references(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QStringList rewritten_lines;
    rewritten_lines.reserve(lines.size());

    bool inside_fenced_block = false;
    for (const QString &line : lines)
    {
        const bool is_fence_line = is_fence_delimiter_line(line);
        if (is_fence_line == true)
        {
            rewritten_lines.append(line);
            inside_fenced_block = !inside_fenced_block;
            continue;
        }

        rewritten_lines.append(inside_fenced_block == true ? line : linkify_non_fenced_line(line));
    }

    return rewritten_lines.join(QLatin1Char('\n'));
}

std::optional<actions_log_file_link_t> parse_actions_log_file_link(const QUrl &url)
{
    if (url.scheme() != QLatin1StringView(actions_log_file_scheme))
    {
        return std::nullopt;
    }

    const QUrlQuery query(url);
    const QString path = query.queryItemValue(QStringLiteral("path"),
                                              QUrl::ComponentFormattingOption::FullyDecoded);
    const int line = query.queryItemValue(QStringLiteral("line")).toInt();
    const int column = query.queryItemValue(QStringLiteral("column")).toInt();
    if (path.trimmed().isEmpty() == true || line <= 0)
    {
        return std::nullopt;
    }

    return actions_log_file_link_t{path, line, column};
}

QString resolve_actions_log_file_link_path(const actions_log_file_link_t &link,
                                           const QString &project_dir)
{
    const QString normalized_path = QDir::fromNativeSeparators(link.path.trimmed());
    if (normalized_path.isEmpty() == true)
    {
        return {};
    }

    QFileInfo absolute_file_info(normalized_path);
    if (absolute_file_info.isAbsolute() == true)
    {
        if (absolute_file_info.exists() == false)
        {
            return {};
        }
        const QString canonical_path = absolute_file_info.canonicalFilePath();
        return canonical_path.isEmpty() == false ? canonical_path
                                                 : absolute_file_info.absoluteFilePath();
    }

    const QString trimmed_project_dir = project_dir.trimmed();
    if (trimmed_project_dir.isEmpty() == true)
    {
        return {};
    }

    const QString absolute_path = QDir(trimmed_project_dir).absoluteFilePath(normalized_path);
    QFileInfo file_info(absolute_path);
    if (file_info.exists() == false)
    {
        return {};
    }

    const QString project_root = QDir(trimmed_project_dir).canonicalPath();
    const QString canonical_path = file_info.canonicalFilePath();
    if (project_root.isEmpty() == false &&
        canonical_path_is_inside_root(canonical_path, project_root) == false)
    {
        return {};
    }

    return canonical_path;
}

}  // namespace qcai2
