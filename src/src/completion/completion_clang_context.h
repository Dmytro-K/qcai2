/*! @file
    @brief Declares shared clang-derived autocomplete context helpers.
*/

#pragma once

#include "../clangd/clangd_service.h"

#include <utils/filepath.h>

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class QTextDocument;

namespace qcai2
{

class clangd_service_t;

/**
 * Describes the best available enclosing class context for one completion position.
 */
struct completion_class_context_t
{
    /** File that contains the class body used for completion context. */
    Utils::FilePath file_path;

    /** Resolved class or struct name. */
    QString class_name;

    /** Namespace/class scope chain that encloses the class. */
    QString enclosing_scope;

    /** One-based inclusive range that covers the class body in file_path. */
    clangd_range_t class_range;

    /**
     * Returns whether this object points at one usable class body.
     * @return True when file_path, class_name, and class_range are all valid.
     */
    bool is_valid() const;
};

/**
 * Describes the owner class inferred from one qualified method signature.
 */
struct completion_owner_symbol_t
{
    /** Fully qualified class name when available. */
    QString qualified_class_name;

    /** Unqualified class name. */
    QString class_name;

    /** Namespace or outer container scope for the class. */
    QString scope;

    /**
     * Returns whether this object points at one usable owner class.
     * @return True when class_name is not empty.
     */
    bool is_valid() const;
};

/**
 * Builds one clang-derived semantic context block for autocomplete requests.
 * @param service Shared clangd integration service. May be null when clangd is unavailable.
 * @param file_path File being completed.
 * @param document Live document snapshot used for line text extraction.
 * @param cursor_position Zero-based document cursor position.
 * @param error Receives a best-effort diagnostic when no semantic context could be produced.
 * @param trace Receives best-effort step-by-step class-context resolution diagnostics.
 * @return Prompt-safe semantic context block, or an empty string when unavailable.
 */
QString build_completion_clang_context_block(clangd_service_t *service,
                                             const Utils::FilePath &file_path,
                                             QTextDocument *document, int cursor_position,
                                             QString *error = nullptr,
                                             QStringList *trace = nullptr);

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

/**
 * Selects the enclosing class context for one completion position.
 * It prefers a class around the current location and can fall back to a related declaration or
 * definition location resolved elsewhere by clangd.
 * @param current_symbols Document symbols for the file currently being completed.
 * @param current_location Current completion location.
 * @param related_symbols Optional document symbols for a related declaration/definition file.
 * @param related_location Optional clangd location inside the related declaration/definition.
 * @return Best available class context, or an invalid object when none could be resolved.
 */
completion_class_context_t
select_completion_class_context(const QList<clangd_document_symbol_t> &current_symbols,
                                const clangd_location_t &current_location,
                                const QList<clangd_document_symbol_t> &related_symbols = {},
                                std::optional<clangd_location_t> related_location = std::nullopt);

/**
 * Returns whether completion context should load a related declaration/definition to recover
 * the owning class.
 * @param current_context Class context resolved directly from the current file.
 * @param function_symbol Current enclosing function or method symbol.
 * @return True when the current file does not already provide a class context and a function is
 *         available for declaration/definition lookup.
 */
bool should_resolve_related_class_context(const completion_class_context_t &current_context,
                                          const clangd_document_symbol_t *function_symbol);

/**
 * Builds the best-effort fully qualified name for one workspace-symbol match.
 * @param match Workspace symbol returned by clangd.
 * @return Qualified symbol name when container data is available, otherwise match.name.
 */
QString workspace_symbol_qualified_name(const clangd_symbol_match_t &match);

/**
 * Returns whether one workspace-symbol match refers to the parsed owner class.
 * @param match Workspace symbol returned by clangd.
 * @param owner_symbol Owner parsed from the current method signature.
 * @return True when the symbol is a class-like match for the owner class.
 */
bool workspace_symbol_matches_owner(const clangd_symbol_match_t &match,
                                    const completion_owner_symbol_t &owner_symbol);

/**
 * Locates one class body directly from file text when clang AST data is unavailable.
 * @param file_path File that should contain the class definition.
 * @param class_name Unqualified class or struct name to search for.
 * @param enclosing_scope Best-effort enclosing scope text for the resulting context.
 * @param anchor_line Preferred one-based line near the expected declaration.
 * @param trace Receives best-effort fallback diagnostics.
 * @return Resolved class context, or an invalid object when text scanning cannot find the class.
 */
completion_class_context_t find_class_context_via_text_scan(const Utils::FilePath &file_path,
                                                            const QString &class_name,
                                                            const QString &enclosing_scope,
                                                            int anchor_line,
                                                            QStringList *trace = nullptr);

/**
 * Extracts the owner class from one method signature or hover label.
 * @param signature_text Method signature text returned by clang.
 * @param fallback_scope Namespace/class scope inferred from the current document-symbol chain.
 * @return Parsed owner class data, or an invalid object when the signature is not qualified.
 */
completion_owner_symbol_t parse_completion_owner_symbol(const QString &signature_text,
                                                        const QString &fallback_scope = {});

}  // namespace qcai2
