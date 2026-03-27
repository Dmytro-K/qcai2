/*! @file
    @brief Declares shared clang-derived autocomplete context helpers.
*/

#pragma once

#include <utils/filepath.h>

#include <QString>

class QTextDocument;

namespace qcai2
{

class clangd_service_t;
struct clangd_range_t;

/**
 * Builds one clang-derived semantic context block for autocomplete requests.
 * @param service Shared clangd integration service. May be null when clangd is unavailable.
 * @param file_path File being completed.
 * @param document Live document snapshot used for line text extraction.
 * @param cursor_position Zero-based document cursor position.
 * @param error Receives a best-effort diagnostic when no semantic context could be produced.
 * @return Prompt-safe semantic context block, or an empty string when unavailable.
 */
QString build_completion_clang_context_block(clangd_service_t *service,
                                             const Utils::FilePath &file_path,
                                             QTextDocument *document, int cursor_position,
                                             QString *error = nullptr);

/**
 * Extracts the leading include-related block from a source document.
 * @param document Live document snapshot to inspect.
 * @param max_scan_lines Maximum number of leading lines to scan.
 * @return Numbered snippet containing the include block, or an empty string when none exists.
 */
QString extract_completion_include_block(QTextDocument *document, int max_scan_lines = 80);

/**
 * Extracts one short numbered snippet for a symbol range when it fits configured limits.
 * @param document Live document snapshot used for snippet extraction.
 * @param range One-based inclusive symbol range.
 * @param max_lines Maximum number of lines allowed in the snippet.
 * @param max_chars Maximum rendered snippet character count allowed.
 * @return Numbered snippet when the range is short enough, or an empty string otherwise.
 */
QString extract_short_completion_symbol_snippet(QTextDocument *document,
                                                const clangd_range_t &range, int max_lines,
                                                int max_chars);

}  // namespace qcai2
