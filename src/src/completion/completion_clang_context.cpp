/*! @file
    @brief Implements shared clang-derived autocomplete context helpers.
*/

#include "completion_clang_context.h"

#include "../clangd/clangd_service.h"

#include <utils/textutils.h>

#include <QTextBlock>
#include <QTextDocument>

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

QString build_completion_clang_context_block(clangd_service_t *service,
                                             const Utils::FilePath &file_path,
                                             QTextDocument *document, int cursor_position,
                                             QString *error)
{
    if (document == nullptr || file_path.isEmpty() == true)
    {
        return {};
    }

    cursor_position = qBound(0, cursor_position, qMax(0, document->characterCount() - 1));
    const Utils::Text::Position text_position =
        Utils::Text::Position::fromPositionInDocument(document, cursor_position);
    if (text_position.isValid() == false)
    {
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

    const QString include_block = extract_completion_include_block(document);
    if (include_block.isEmpty() == false)
    {
        sections.append(
            QStringLiteral("Leading include block\n~~~~text\n%1\n~~~~").arg(include_block));
    }

    if (service == nullptr)
    {
        return sections.size() > 1 ? sections.join(QStringLiteral("\n\n")) : QString();
    }

    QString symbols_error;
    const QList<clangd_document_symbol_t> symbols =
        service->document_symbols(file_path, &symbols_error);
    const QList<const clangd_document_symbol_t *> chain = find_symbol_chain(symbols, location);
    const clangd_document_symbol_t *function_symbol = last_symbol_matching(
        chain, [](const clangd_document_symbol_t &symbol) { return is_function_like(symbol); });
    const clangd_document_symbol_t *class_symbol = last_symbol_matching(
        chain, [](const clangd_document_symbol_t &symbol) { return is_class_like(symbol); });

    const QString enclosing_scope = enclosing_scope_text(chain);
    if (enclosing_scope.isEmpty() == false)
    {
        sections.append(
            QStringLiteral("Enclosing class and namespace\n- %1").arg(enclosing_scope));
    }
    if (class_symbol != nullptr)
    {
        sections.append(QStringLiteral("Current class\n- %1").arg(class_symbol->name));
        const QString class_body = extract_short_completion_symbol_snippet(
            document, class_symbol->range, k_max_class_snippet_lines, k_max_class_snippet_chars);
        if (class_body.isEmpty() == false)
        {
            sections.append(
                QStringLiteral("Current class body (short)\n~~~~text\n%1\n~~~~").arg(class_body));
        }
    }

    if (function_symbol != nullptr)
    {
        const QString signature_text =
            function_signature_text(service, file_path, function_symbol);
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
        if (error != nullptr && symbols_error.isEmpty() == false)
        {
            *error = symbols_error;
        }
        return {};
    }

    return sections.join(QStringLiteral("\n\n"));
}

}  // namespace qcai2
