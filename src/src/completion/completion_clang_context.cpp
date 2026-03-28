/*! @file
    @brief Implements shared clang-derived autocomplete context helpers.
*/

#include "completion_clang_context.h"

#include "../clangd/clangd_service.h"

#include <utils/textutils.h>

#include <QFile>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

#include <limits>
#include <optional>

namespace qcai2
{

namespace
{

constexpr int function_snippet_lines_max = QCAI2_COMPLETION_FUNCTION_SNIPPET_LINES_MAX;
constexpr int function_snippet_chars_max = QCAI2_COMPLETION_FUNCTION_SNIPPET_CHARS_MAX;
constexpr int class_snippet_lines_max = QCAI2_COMPLETION_CLASS_SNIPPET_LINES_MAX;
constexpr int class_snippet_chars_max = QCAI2_COMPLETION_CLASS_SNIPPET_CHARS_MAX;

bool is_function_like(const clangd_document_symbol_t &symbol)
{
    return symbol.kind_name == QStringLiteral("function") ||
           symbol.kind_name == QStringLiteral("method") ||
           symbol.kind_name == QStringLiteral("constructor");
}

bool is_namespace_or_class_like(const clangd_document_symbol_t &symbol)
{
    return symbol.kind_name == QStringLiteral("namespace") ||
           symbol.kind_name == QStringLiteral("class") ||
           symbol.kind_name == QStringLiteral("struct") ||
           symbol.kind_name == QStringLiteral("interface") ||
           symbol.kind_name == QStringLiteral("enum");
}

bool is_class_like(const clangd_document_symbol_t &symbol)
{
    return symbol.kind_name == QStringLiteral("class") ||
           symbol.kind_name == QStringLiteral("struct") ||
           symbol.kind_name == QStringLiteral("interface");
}

bool is_class_like_kind(const QString &kind_name)
{
    return kind_name == QStringLiteral("class") || kind_name == QStringLiteral("struct") ||
           kind_name == QStringLiteral("interface");
}

void append_clang_context_trace(QStringList *trace, const QString &message)
{
    if (trace != nullptr)
    {
        trace->append(message);
    }
}

bool find_symbol_chain_recursive(const clangd_document_symbol_t &symbol,
                                 const clangd_location_t &location,
                                 QList<const clangd_document_symbol_t *> *chain)
{
    const clangd_range_t probe_range =
        symbol.selection_range.is_valid() == true ? symbol.selection_range : symbol.range;
    if (probe_range.contains(location.line, location.column) == false &&
        symbol.range.contains(location.line, location.column) == false)
    {
        return false;
    }

    chain->append(&symbol);
    for (const clangd_document_symbol_t &child : symbol.children)
    {
        if (find_symbol_chain_recursive(child, location, chain) == true)
        {
            return true;
        }
    }
    return true;
}

QList<const clangd_document_symbol_t *>
find_symbol_chain(const QList<clangd_document_symbol_t> &symbols,
                  const clangd_location_t &location)
{
    QList<const clangd_document_symbol_t *> chain;
    for (const clangd_document_symbol_t &symbol : symbols)
    {
        QList<const clangd_document_symbol_t *> candidate;
        if (find_symbol_chain_recursive(symbol, location, &candidate) == true)
        {
            chain = candidate;
        }
    }
    return chain;
}

const clangd_document_symbol_t *
last_symbol_matching(const QList<const clangd_document_symbol_t *> &chain,
                     const std::function<bool(const clangd_document_symbol_t &)> &predicate)
{
    for (qsizetype index = chain.size() - 1; index >= 0; --index)
    {
        const clangd_document_symbol_t *symbol = chain.at(static_cast<int>(index));
        if (symbol != nullptr && predicate(*symbol) == true)
        {
            return symbol;
        }
    }
    return nullptr;
}

QString numbered_document_snippet(QTextDocument *document, int start_line, int end_line)
{
    if (document == nullptr || start_line <= 0 || end_line < start_line)
    {
        return {};
    }

    QStringList snippet_lines;
    snippet_lines.reserve(end_line - start_line + 1);
    for (int line = start_line; line <= end_line; ++line)
    {
        const QTextBlock block = document->findBlockByNumber(line - 1);
        if (block.isValid() == false)
        {
            break;
        }
        snippet_lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(block.text()));
    }
    return snippet_lines.join(QLatin1Char('\n'));
}

QString sanitize_signature_text(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("```")) == true)
    {
        const qsizetype first_newline = text.indexOf(QLatin1Char('\n'));
        if (first_newline >= 0)
        {
            text = text.mid(first_newline + 1).trimmed();
        }
        if (text.endsWith(QStringLiteral("```")) == true)
        {
            text.chop(3);
            text = text.trimmed();
        }
    }

    const qsizetype first_blank_line = text.indexOf(QStringLiteral("\n\n"));
    if (first_blank_line >= 0)
    {
        text = text.left(first_blank_line).trimmed();
    }

    return text;
}

QString signature_code_text(const QString &signature_text)
{
    const QRegularExpression code_re(QStringLiteral(R"(`([^`\n]+)`)"));
    const QRegularExpressionMatch match = code_re.match(signature_text);
    QString code = match.hasMatch() == true ? match.captured(1) : signature_text;

    const qsizetype paren_index = code.indexOf(QLatin1Char('('));
    if (paren_index >= 0)
    {
        code = code.left(paren_index);
    }

    return code.trimmed();
}

QString function_signature_text(clangd_service_t *service, const Utils::FilePath &file_path,
                                const clangd_document_symbol_t *function_symbol)
{
    if (service == nullptr || function_symbol == nullptr)
    {
        return {};
    }

    const clangd_range_t probe_range = function_symbol->selection_range.is_valid() == true
                                           ? function_symbol->selection_range
                                           : function_symbol->range;
    QString location_error;
    const std::optional<clangd_location_t> location = service->resolve_location(
        file_path, probe_range.start_line, probe_range.start_column, &location_error);
    if (location.has_value() == true)
    {
        const QString hover_text =
            sanitize_signature_text(service->hover_text_at(location.value()));
        if (hover_text.isEmpty() == false)
        {
            return hover_text;
        }
    }

    QString signature = function_symbol->name;
    if (function_symbol->detail.isEmpty() == false)
    {
        signature += QStringLiteral(" — %1").arg(function_symbol->detail);
    }
    return signature;
}

QString enclosing_scope_text(const QList<const clangd_document_symbol_t *> &chain)
{
    QStringList parts;
    for (const clangd_document_symbol_t *symbol : chain)
    {
        if (symbol != nullptr && is_namespace_or_class_like(*symbol) == true)
        {
            parts.append(symbol->name);
        }
    }
    return parts.join(QStringLiteral("::"));
}

completion_class_context_t
class_context_from_chain(const QList<const clangd_document_symbol_t *> &chain,
                         const Utils::FilePath &file_path)
{
    completion_class_context_t context;
    const clangd_document_symbol_t *class_symbol = last_symbol_matching(
        chain, [](const clangd_document_symbol_t &symbol) { return is_class_like(symbol); });
    if (class_symbol == nullptr)
    {
        return context;
    }

    context.file_path = file_path;
    context.class_name = class_symbol->name;
    context.enclosing_scope = enclosing_scope_text(chain);
    context.class_range = class_symbol->range;
    return context;
}

completion_class_context_t
find_class_context_via_workspace_search(clangd_service_t *service,
                                        const completion_owner_symbol_t &owner_symbol,
                                        QStringList *trace = nullptr)
{
    if (service == nullptr || owner_symbol.is_valid() == false)
    {
        append_clang_context_trace(
            trace, QStringLiteral("workspace_search skipped: service=%1 owner_valid=%2")
                       .arg(service != nullptr ? QStringLiteral("yes") : QStringLiteral("no"),
                            owner_symbol.is_valid() == true ? QStringLiteral("yes")
                                                            : QStringLiteral("no")));
        return {};
    }

    QStringList queries;
    if (owner_symbol.qualified_class_name.isEmpty() == false)
    {
        queries.append(owner_symbol.qualified_class_name);
    }
    queries.append(owner_symbol.class_name);
    queries.removeDuplicates();

    std::optional<clangd_symbol_match_t> best_match;
    int best_score = std::numeric_limits<int>::min();

    for (const QString &query : queries)
    {
        QString search_error;
        const QList<clangd_symbol_match_t> matches =
            service->search_symbols(query, 32, &search_error);
        append_clang_context_trace(
            trace,
            QStringLiteral("workspace_search query=`%1` results=%2 error=%3")
                .arg(query)
                .arg(matches.size())
                .arg(search_error.isEmpty() == true ? QStringLiteral("<none>") : search_error));
        for (const clangd_symbol_match_t &match : matches)
        {
            append_clang_context_trace(
                trace,
                QStringLiteral("workspace_search candidate name=`%1` container=`%2` kind=%3 "
                               "matches_owner=%4")
                    .arg(match.name, match.container_name, match.kind_name,
                         workspace_symbol_matches_owner(match, owner_symbol) == true
                             ? QStringLiteral("yes")
                             : QStringLiteral("no")));
            if (workspace_symbol_matches_owner(match, owner_symbol) == false)
            {
                continue;
            }

            int score = 100;
            const QString qualified_name = workspace_symbol_qualified_name(match);
            if (match.name == owner_symbol.qualified_class_name ||
                qualified_name == owner_symbol.qualified_class_name)
            {
                score += 100;
            }
            else if (match.name == owner_symbol.class_name)
            {
                score += 25;
            }
            if (owner_symbol.scope.isEmpty() == false)
            {
                if (match.container_name == owner_symbol.scope)
                {
                    score += 50;
                }
                else if (qualified_name.startsWith(owner_symbol.scope + QStringLiteral("::")) ==
                         true)
                {
                    score += 40;
                }
                else if (match.container_name.endsWith(
                             QStringLiteral("::%1").arg(owner_symbol.scope)))
                {
                    score += 25;
                }
            }

            if (score > best_score)
            {
                best_score = score;
                best_match = match;
            }
        }
    }

    if (best_match.has_value() == false)
    {
        append_clang_context_trace(trace, QStringLiteral("workspace_search best_match=<none>"));
        return {};
    }

    append_clang_context_trace(
        trace, QStringLiteral("workspace_search best_match name=`%1` container=`%2` file=%3:%4:%5")
                   .arg(best_match->name, best_match->container_name,
                        best_match->link.file_path.toUrlishString())
                   .arg(best_match->link.line)
                   .arg(best_match->link.column));

    QString location_error;
    const std::optional<clangd_location_t> class_location =
        service->resolve_location(best_match->link.file_path, best_match->link.line,
                                  best_match->link.column, &location_error);
    if (class_location.has_value() == false)
    {
        append_clang_context_trace(
            trace, QStringLiteral("workspace_search resolve_location failed: %1")
                       .arg(location_error.isEmpty() == true ? QStringLiteral("<none>")
                                                             : location_error));
        return {};
    }

    append_clang_context_trace(trace,
                               QStringLiteral("workspace_search resolved_location file=%1:%2:%3")
                                   .arg(class_location->file_path.toUrlishString())
                                   .arg(class_location->line)
                                   .arg(class_location->column));

    QString symbols_error;
    const QList<clangd_document_symbol_t> class_file_symbols =
        service->document_symbols(class_location->file_path, &symbols_error);
    append_clang_context_trace(
        trace,
        QStringLiteral("workspace_search class_file_symbols count=%1 error=%2")
            .arg(class_file_symbols.size())
            .arg(symbols_error.isEmpty() == true ? QStringLiteral("<none>") : symbols_error));
    if (class_file_symbols.isEmpty() == true)
    {
        const completion_class_context_t text_context = find_class_context_via_text_scan(
            class_location->file_path, owner_symbol.class_name, owner_symbol.qualified_class_name,
            class_location->line, trace);
        append_clang_context_trace(
            trace,
            QStringLiteral("workspace_search text_scan_context valid=%1 class=`%2` scope=`%3`")
                .arg(text_context.is_valid() == true ? QStringLiteral("yes")
                                                     : QStringLiteral("no"),
                     text_context.class_name, text_context.enclosing_scope));
        if (text_context.is_valid() == true)
        {
            return text_context;
        }
    }
    const QList<const clangd_document_symbol_t *> class_chain =
        find_symbol_chain(class_file_symbols, *class_location);
    const completion_class_context_t context =
        class_context_from_chain(class_chain, class_location->file_path);
    append_clang_context_trace(
        trace, QStringLiteral("workspace_search class_context valid=%1 class=`%2` scope=`%3`")
                   .arg(context.is_valid() == true ? QStringLiteral("yes") : QStringLiteral("no"),
                        context.class_name, context.enclosing_scope));
    return context;
}

bool looks_like_include_preamble_line(const QString &trimmed_line)
{
    return trimmed_line.startsWith(QStringLiteral("#pragma once")) == true ||
           trimmed_line.startsWith(QStringLiteral("#include")) == true ||
           trimmed_line.startsWith(QStringLiteral("#if")) == true ||
           trimmed_line.startsWith(QStringLiteral("#ifdef")) == true ||
           trimmed_line.startsWith(QStringLiteral("#ifndef")) == true ||
           trimmed_line.startsWith(QStringLiteral("#define")) == true;
}

}  // namespace

QString extract_short_completion_symbol_snippet(QTextDocument *document,
                                                const clangd_range_t &range, const int max_lines,
                                                const int max_chars)
{
    if (document == nullptr || range.is_valid() == false || max_lines <= 0 || max_chars <= 0)
    {
        return {};
    }

    const int line_count = range.end_line - range.start_line + 1;
    if (line_count <= 0 || line_count > max_lines)
    {
        return {};
    }

    const QString snippet = numbered_document_snippet(document, range.start_line, range.end_line);
    return snippet.size() <= max_chars ? snippet : QString();
}

QString extract_completion_include_block(QTextDocument *document, const int max_scan_lines)
{
    if (document == nullptr || max_scan_lines <= 0)
    {
        return {};
    }

    QStringList lines;
    bool in_block_comment = false;
    bool has_include_line = false;
    bool saw_relevant_line = false;
    const int line_limit = qMin(max_scan_lines, document->blockCount());
    for (int line = 1; line <= line_limit; ++line)
    {
        const QTextBlock block = document->findBlockByNumber(line - 1);
        if (block.isValid() == false)
        {
            break;
        }

        const QString text = block.text();
        const QString trimmed = text.trimmed();
        if (in_block_comment == true)
        {
            if (saw_relevant_line == true)
            {
                lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(text));
            }
            if (trimmed.contains(QStringLiteral("*/")) == true)
            {
                in_block_comment = false;
            }
            continue;
        }

        if (trimmed.startsWith(QStringLiteral("/*")) == true)
        {
            if (saw_relevant_line == true)
            {
                lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(text));
            }
            if (trimmed.contains(QStringLiteral("*/")) == false)
            {
                in_block_comment = true;
            }
            continue;
        }

        if (trimmed.startsWith(QStringLiteral("//")) == true)
        {
            if (saw_relevant_line == true)
            {
                lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(text));
            }
            continue;
        }

        if (trimmed.isEmpty() == true)
        {
            if (saw_relevant_line == true)
            {
                lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(text));
            }
            continue;
        }

        if (looks_like_include_preamble_line(trimmed) == true)
        {
            saw_relevant_line = true;
            has_include_line = has_include_line || trimmed.startsWith(QStringLiteral("#include"));
            lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(text));
            continue;
        }

        if (saw_relevant_line == true)
        {
            break;
        }
    }

    while (lines.isEmpty() == false && lines.last().trimmed().isEmpty() == true)
    {
        lines.removeLast();
    }

    return has_include_line == true ? lines.join(QLatin1Char('\n')) : QString();
}

bool completion_class_context_t::is_valid() const
{
    return this->file_path.isEmpty() == false && this->class_name.isEmpty() == false &&
           this->class_range.is_valid() == true;
}

bool completion_owner_symbol_t::is_valid() const
{
    return this->class_name.isEmpty() == false;
}

bool should_resolve_related_class_context(const completion_class_context_t &current_context,
                                          const clangd_document_symbol_t *function_symbol)
{
    return current_context.is_valid() == false && function_symbol != nullptr;
}

QString workspace_symbol_qualified_name(const clangd_symbol_match_t &match)
{
    if (match.container_name.isEmpty() == true)
    {
        return match.name;
    }
    if (match.name.startsWith(match.container_name + QStringLiteral("::")) == true)
    {
        return match.name;
    }
    return QStringLiteral("%1::%2").arg(match.container_name, match.name);
}

bool workspace_symbol_matches_owner(const clangd_symbol_match_t &match,
                                    const completion_owner_symbol_t &owner_symbol)
{
    if (owner_symbol.is_valid() == false || is_class_like_kind(match.kind_name) == false ||
        match.link.is_valid() == false)
    {
        return false;
    }

    const QString qualified_name = workspace_symbol_qualified_name(match);
    return match.name == owner_symbol.class_name ||
           match.name == owner_symbol.qualified_class_name ||
           qualified_name == owner_symbol.qualified_class_name ||
           match.name.endsWith(QStringLiteral("::%1").arg(owner_symbol.class_name)) == true ||
           qualified_name.endsWith(QStringLiteral("::%1").arg(owner_symbol.class_name)) == true;
}

completion_class_context_t find_class_context_via_text_scan(const Utils::FilePath &file_path,
                                                            const QString &class_name,
                                                            const QString &enclosing_scope,
                                                            int anchor_line, QStringList *trace)
{
    completion_class_context_t context;
    if (file_path.isEmpty() == true || class_name.isEmpty() == true)
    {
        append_clang_context_trace(
            trace,
            QStringLiteral("text_scan skipped file_empty=%1 class_empty=%2")
                .arg(file_path.isEmpty() == true ? QStringLiteral("yes") : QStringLiteral("no"),
                     class_name.isEmpty() == true ? QStringLiteral("yes") : QStringLiteral("no")));
        return context;
    }

    QFile file(file_path.toFSPathString());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        append_clang_context_trace(
            trace,
            QStringLiteral("text_scan open_failed path=%1").arg(file_path.toUrlishString()));
        return context;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split(QLatin1Char('\n'));
    const QRegularExpression declaration_re(QStringLiteral(R"(\b(class|struct|interface)\s+%1\b)")
                                                .arg(QRegularExpression::escape(class_name)));

    int start_line = -1;
    int start_distance = std::numeric_limits<int>::max();
    for (int index = 0; index < lines.size(); ++index)
    {
        if (declaration_re.match(lines.at(index)).hasMatch() == false)
        {
            continue;
        }

        const int line_number = index + 1;
        const int distance = anchor_line > 0 ? qAbs(line_number - anchor_line) : line_number;
        if (distance < start_distance)
        {
            start_distance = distance;
            start_line = line_number;
        }
    }

    if (start_line <= 0)
    {
        append_clang_context_trace(
            trace, QStringLiteral("text_scan declaration_not_found class=`%1` file=%2")
                       .arg(class_name, file_path.toUrlishString()));
        return context;
    }

    int brace_depth = 0;
    bool seen_open_brace = false;
    int end_line = -1;
    for (int line_index = start_line - 1; line_index < lines.size(); ++line_index)
    {
        const QString &line = lines.at(line_index);
        for (const QChar ch : line)
        {
            if (ch == QLatin1Char('{'))
            {
                ++brace_depth;
                seen_open_brace = true;
            }
            else if (ch == QLatin1Char('}') && brace_depth > 0)
            {
                --brace_depth;
                if (seen_open_brace == true && brace_depth == 0)
                {
                    end_line = line_index + 1;
                    break;
                }
            }
        }
        if (end_line > 0)
        {
            break;
        }
    }

    if (seen_open_brace == false || end_line <= 0)
    {
        append_clang_context_trace(
            trace, QStringLiteral("text_scan body_not_found class=`%1` start_line=%2")
                       .arg(class_name)
                       .arg(start_line));
        return context;
    }

    context.file_path = file_path;
    context.class_name = class_name;
    context.enclosing_scope = enclosing_scope;
    context.class_range.start_line = start_line;
    context.class_range.start_column = 1;
    context.class_range.end_line = end_line;
    context.class_range.end_column =
        qMax(1, static_cast<int>(lines.value(end_line - 1).size()) + 1);
    append_clang_context_trace(trace,
                               QStringLiteral("text_scan resolved class=`%1` range=%2:%3-%4:%5")
                                   .arg(class_name)
                                   .arg(context.class_range.start_line)
                                   .arg(context.class_range.start_column)
                                   .arg(context.class_range.end_line)
                                   .arg(context.class_range.end_column));
    return context;
}

completion_class_context_t
select_completion_class_context(const QList<clangd_document_symbol_t> &current_symbols,
                                const clangd_location_t &current_location,
                                const QList<clangd_document_symbol_t> &related_symbols,
                                std::optional<clangd_location_t> related_location)
{
    const QList<const clangd_document_symbol_t *> current_chain =
        find_symbol_chain(current_symbols, current_location);
    completion_class_context_t current_context =
        class_context_from_chain(current_chain, current_location.file_path);
    if (current_context.is_valid() == true)
    {
        return current_context;
    }

    if (related_location.has_value() == false)
    {
        return {};
    }

    const QList<const clangd_document_symbol_t *> related_chain =
        find_symbol_chain(related_symbols, *related_location);
    return class_context_from_chain(related_chain, related_location->file_path);
}

completion_owner_symbol_t parse_completion_owner_symbol(const QString &signature_text,
                                                        const QString &fallback_scope)
{
    completion_owner_symbol_t owner_symbol;
    QString owner = signature_code_text(signature_text);
    const qsizetype qualifier_index = owner.lastIndexOf(QStringLiteral("::"));
    if (qualifier_index < 0)
    {
        return owner_symbol;
    }

    owner = owner.left(qualifier_index).trimmed();
    if (owner.contains(QLatin1Char(' ')) == true)
    {
        owner = owner.section(QLatin1Char(' '), -1).trimmed();
    }
    while (owner.startsWith(QStringLiteral("::")) == true)
    {
        owner.remove(0, 2);
    }
    if (owner.isEmpty() == true)
    {
        return owner_symbol;
    }

    owner_symbol.class_name = owner.section(QStringLiteral("::"), -1);
    if (owner.contains(QStringLiteral("::")) == true)
    {
        owner_symbol.scope = owner.section(QStringLiteral("::"), 0, -2);
        owner_symbol.qualified_class_name = owner;
    }
    else
    {
        owner_symbol.scope = fallback_scope;
        owner_symbol.qualified_class_name =
            fallback_scope.isEmpty() == true ? owner
                                             : QStringLiteral("%1::%2").arg(fallback_scope, owner);
    }
    return owner_symbol;
}

QString build_completion_clang_context_block(clangd_service_t *service,
                                             const Utils::FilePath &file_path,
                                             QTextDocument *document, int cursor_position,
                                             QString *error, QStringList *trace)
{
    if (document == nullptr || file_path.isEmpty() == true)
    {
        append_clang_context_trace(
            trace,
            QStringLiteral("build skipped: document=%1 file_path_empty=%2")
                .arg(document != nullptr ? QStringLiteral("yes") : QStringLiteral("no"),
                     file_path.isEmpty() == true ? QStringLiteral("yes") : QStringLiteral("no")));
        return {};
    }

    cursor_position = qBound(0, cursor_position, qMax(0, document->characterCount() - 1));
    const Utils::Text::Position text_position =
        Utils::Text::Position::fromPositionInDocument(document, cursor_position);
    if (text_position.isValid() == false)
    {
        append_clang_context_trace(trace, QStringLiteral("cursor_position invalid"));
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to resolve the autocomplete cursor position.");
        }
        return {};
    }

    const clangd_location_t location{
        file_path,
        LanguageServerProtocol::Position(text_position.line - 1, text_position.column),
        text_position.line,
        text_position.column + 1,
    };

    QStringList sections;
    sections.append(QStringLiteral("Local C++ semantic context"));
    append_clang_context_trace(trace,
                               QStringLiteral("build started file=%1 cursor=%2 line=%3 column=%4")
                                   .arg(file_path.toUrlishString())
                                   .arg(cursor_position)
                                   .arg(text_position.line)
                                   .arg(text_position.column + 1));

    const QString include_block = extract_completion_include_block(document);
    if (include_block.isEmpty() == false)
    {
        sections.append(
            QStringLiteral("Leading include block\n~~~~text\n%1\n~~~~").arg(include_block));
        append_clang_context_trace(
            trace, QStringLiteral("include_block loaded chars=%1").arg(include_block.size()));
    }
    else
    {
        append_clang_context_trace(trace, QStringLiteral("include_block empty"));
    }

    if (service == nullptr)
    {
        append_clang_context_trace(trace, QStringLiteral("clangd service unavailable"));
        return sections.size() > 1 ? sections.join(QStringLiteral("\n\n")) : QString();
    }

    QString symbols_error;
    const QList<clangd_document_symbol_t> symbols =
        service->document_symbols(file_path, &symbols_error);
    append_clang_context_trace(
        trace,
        QStringLiteral("current_file_symbols count=%1 error=%2")
            .arg(symbols.size())
            .arg(symbols_error.isEmpty() == true ? QStringLiteral("<none>") : symbols_error));
    const QList<const clangd_document_symbol_t *> chain = find_symbol_chain(symbols, location);
    append_clang_context_trace(trace,
                               QStringLiteral("current_symbol_chain depth=%1").arg(chain.size()));
    const clangd_document_symbol_t *function_symbol = last_symbol_matching(
        chain, [](const clangd_document_symbol_t &symbol) { return is_function_like(symbol); });
    const completion_class_context_t current_class_context =
        class_context_from_chain(chain, file_path);
    append_clang_context_trace(
        trace, QStringLiteral("current_class_context valid=%1 class=`%2` scope=`%3`")
                   .arg(current_class_context.is_valid() == true ? QStringLiteral("yes")
                                                                 : QStringLiteral("no"),
                        current_class_context.class_name, current_class_context.enclosing_scope));
    const QString direct_enclosing_scope = enclosing_scope_text(chain);
    const QString signature_text =
        function_symbol != nullptr ? function_signature_text(service, file_path, function_symbol)
                                   : QString();
    append_clang_context_trace(
        trace,
        QStringLiteral("function_symbol present=%1 name=`%2` signature=`%3` direct_scope=`%4`")
            .arg(function_symbol != nullptr ? QStringLiteral("yes") : QStringLiteral("no"),
                 function_symbol != nullptr ? function_symbol->name : QString(), signature_text,
                 direct_enclosing_scope));
    const completion_owner_symbol_t owner_symbol =
        parse_completion_owner_symbol(signature_text, direct_enclosing_scope);
    append_clang_context_trace(
        trace,
        QStringLiteral("owner_symbol valid=%1 class=`%2` scope=`%3` qualified=`%4`")
            .arg(owner_symbol.is_valid() == true ? QStringLiteral("yes") : QStringLiteral("no"),
                 owner_symbol.class_name, owner_symbol.scope, owner_symbol.qualified_class_name));

    QList<clangd_document_symbol_t> related_symbols;
    std::optional<clangd_location_t> related_location;
    if (should_resolve_related_class_context(current_class_context, function_symbol) == true)
    {
        const clangd_range_t probe_range = function_symbol->selection_range.is_valid() == true
                                               ? function_symbol->selection_range
                                               : function_symbol->range;
        QString function_location_error;
        const std::optional<clangd_location_t> function_location = service->resolve_location(
            file_path, probe_range.start_line, probe_range.start_column, &function_location_error);
        append_clang_context_trace(
            trace, QStringLiteral("related_lookup function_location success=%1 error=%2")
                       .arg(function_location.has_value() == true ? QStringLiteral("yes")
                                                                  : QStringLiteral("no"),
                            function_location_error.isEmpty() == true ? QStringLiteral("<none>")
                                                                      : function_location_error));
        if (function_location.has_value() == true)
        {
            QString related_error;
            const std::optional<clangd_link_t> related_link =
                service->switch_decl_def(*function_location, &related_error);
            append_clang_context_trace(
                trace, QStringLiteral("related_lookup switch_decl_def success=%1 error=%2")
                           .arg(related_link.has_value() == true ? QStringLiteral("yes")
                                                                 : QStringLiteral("no"),
                                related_error.isEmpty() == true ? QStringLiteral("<none>")
                                                                : related_error));
            if (related_link.has_value() == true)
            {
                append_clang_context_trace(trace,
                                           QStringLiteral("related_lookup target=%1:%2:%3")
                                               .arg(related_link->file_path.toUrlishString())
                                               .arg(related_link->line)
                                               .arg(related_link->column));
                QString related_location_error;
                related_location =
                    service->resolve_location(related_link->file_path, related_link->line,
                                              related_link->column, &related_location_error);
                append_clang_context_trace(
                    trace,
                    QStringLiteral("related_lookup resolve_location success=%1 error=%2")
                        .arg(related_location.has_value() == true ? QStringLiteral("yes")
                                                                  : QStringLiteral("no"),
                             related_location_error.isEmpty() == true ? QStringLiteral("<none>")
                                                                      : related_location_error));
                if (related_location.has_value() == true)
                {
                    QString related_symbols_error;
                    related_symbols = service->document_symbols(related_location->file_path,
                                                                &related_symbols_error);
                    append_clang_context_trace(
                        trace,
                        QStringLiteral("related_lookup symbols count=%1 error=%2")
                            .arg(related_symbols.size())
                            .arg(related_symbols_error.isEmpty() == true ? QStringLiteral("<none>")
                                                                         : related_symbols_error));
                }
            }
        }
    }
    else
    {
        append_clang_context_trace(
            trace,
            QStringLiteral("related_lookup skipped current_class_valid=%1 function_present=%2")
                .arg(current_class_context.is_valid() == true ? QStringLiteral("yes")
                                                              : QStringLiteral("no"),
                     function_symbol != nullptr ? QStringLiteral("yes") : QStringLiteral("no")));
    }

    completion_class_context_t class_context =
        current_class_context.is_valid() == true
            ? current_class_context
            : select_completion_class_context(symbols, location, related_symbols,
                                              related_location);
    if (class_context.is_valid() == false && related_location.has_value() == true &&
        owner_symbol.is_valid() == true)
    {
        class_context = find_class_context_via_text_scan(
            related_location->file_path, owner_symbol.class_name,
            owner_symbol.qualified_class_name, related_location->line, trace);
        append_clang_context_trace(
            trace,
            QStringLiteral("related_lookup text_scan_context valid=%1 class=`%2` scope=`%3`")
                .arg(class_context.is_valid() == true ? QStringLiteral("yes")
                                                      : QStringLiteral("no"),
                     class_context.class_name, class_context.enclosing_scope));
    }
    append_clang_context_trace(
        trace,
        QStringLiteral("selected_class_context valid=%1 class=`%2` scope=`%3` file=%4")
            .arg(class_context.is_valid() == true ? QStringLiteral("yes") : QStringLiteral("no"),
                 class_context.class_name, class_context.enclosing_scope,
                 class_context.file_path.toUrlishString()));
    if (class_context.is_valid() == false)
    {
        class_context = find_class_context_via_workspace_search(service, owner_symbol, trace);
        append_clang_context_trace(
            trace, QStringLiteral("workspace_class_context valid=%1 class=`%2` scope=`%3` file=%4")
                       .arg(class_context.is_valid() == true ? QStringLiteral("yes")
                                                             : QStringLiteral("no"),
                            class_context.class_name, class_context.enclosing_scope,
                            class_context.file_path.toUrlishString()));
    }

    const QString enclosing_scope =
        class_context.is_valid() == true ? class_context.enclosing_scope : direct_enclosing_scope;
    if (enclosing_scope.isEmpty() == false)
    {
        sections.append(
            QStringLiteral("Enclosing class and namespace\n- %1").arg(enclosing_scope));
    }
    if (class_context.is_valid() == true)
    {
        sections.append(QStringLiteral("Current class\n- %1").arg(class_context.class_name));

        QString class_body;
        if (class_context.file_path == file_path)
        {
            class_body = extract_short_completion_symbol_snippet(
                document, class_context.class_range, class_snippet_lines_max,
                class_snippet_chars_max);
            append_clang_context_trace(
                trace, QStringLiteral("class_body same_file chars=%1").arg(class_body.size()));
        }
        else
        {
            const int line_count =
                class_context.class_range.end_line - class_context.class_range.start_line + 1;
            if (line_count > 0 && line_count <= class_snippet_lines_max)
            {
                QString snippet_error;
                const QString snippet = service->source_snippet(
                    class_context.file_path, class_context.class_range.start_line,
                    class_context.class_range.end_line, &snippet_error);
                if (snippet.size() <= class_snippet_chars_max)
                {
                    class_body = snippet;
                }
                append_clang_context_trace(
                    trace, QStringLiteral("class_body cross_file lines=%1 chars=%2 accepted=%3 "
                                          "error=%4")
                               .arg(line_count)
                               .arg(snippet.size())
                               .arg(class_body.isEmpty() == false ? QStringLiteral("yes")
                                                                  : QStringLiteral("no"))
                               .arg(snippet_error.isEmpty() == true ? QStringLiteral("<none>")
                                                                    : snippet_error));
            }
            else
            {
                append_clang_context_trace(
                    trace, QStringLiteral("class_body cross_file skipped line_count=%1 max=%2")
                               .arg(line_count)
                               .arg(class_snippet_lines_max));
            }
        }
        if (class_body.isEmpty() == false)
        {
            sections.append(
                QStringLiteral("Current class body (short)\n~~~~text\n%1\n~~~~").arg(class_body));
        }
    }

    if (function_symbol != nullptr)
    {
        if (signature_text.isEmpty() == false)
        {
            sections.append(
                QStringLiteral("Current function signature\n- %1").arg(signature_text));
        }

        const QString function_body = extract_short_completion_symbol_snippet(
            document, function_symbol->range, function_snippet_lines_max,
            function_snippet_chars_max);
        if (function_body.isEmpty() == false)
        {
            sections.append(QStringLiteral("Current function body (short)\n~~~~text\n%1\n~~~~")
                                .arg(function_body));
        }
    }

    if (sections.size() <= 1)
    {
        append_clang_context_trace(trace, QStringLiteral("result empty"));
        if (error != nullptr && symbols_error.isEmpty() == false)
        {
            *error = symbols_error;
        }
        return {};
    }

    append_clang_context_trace(trace,
                               QStringLiteral("result loaded sections=%1").arg(sections.size()));
    return sections.join(QStringLiteral("\n\n"));
}

}  // namespace qcai2
