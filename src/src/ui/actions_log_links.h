/*! @file
    @brief Declares helpers for clickable file-location links in the Actions Log.
*/
#pragma once

#include <QString>
#include <QUrl>

#include <optional>

namespace qcai2
{

/**
 * One parsed internal Actions Log file link.
 */
struct actions_log_file_link_t
{
    /** Original path text stored in the link payload. */
    QString path;

    /** One-based line number requested by the log entry. */
    int line = 0;

    /** Optional one-based column number requested by the log entry. */
    int column = 0;
};

/**
 * Rewrites plain `path:line` mentions in markdown into internal clickable links.
 * Fenced code blocks and existing markdown links are left untouched.
 * @param markdown Source markdown shown in the Actions Log.
 * @return Markdown with internal file-location links for render-only use.
 */
QString linkify_actions_log_file_references(const QString &markdown);

/**
 * Parses one internal Actions Log link back into its file-location payload.
 * @param url Internal URL emitted by linkify_actions_log_file_references().
 * @return Parsed file-location payload when the URL uses the expected scheme.
 */
std::optional<actions_log_file_link_t> parse_actions_log_file_link(const QUrl &url);

/**
 * Resolves one parsed file-location link to an existing absolute file path.
 * Relative paths are resolved under the current project directory and must stay inside it.
 * Absolute paths are accepted as-is when the file exists.
 * @param link Parsed file-location payload.
 * @param project_dir Current project directory used for relative links.
 * @return Canonical absolute path, or an empty string when the target is invalid.
 */
QString resolve_actions_log_file_link_path(const actions_log_file_link_t &link,
                                           const QString &project_dir);

}  // namespace qcai2
