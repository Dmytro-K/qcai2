#include "clangd_tools.h"

#include "../clangd/clangd_service.h"
#include "../util/process_runner.h"
#include "tool_registry.h"

#include <coreplugin/editormanager/editormanager.h>
#include <utils/link.h>

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include <functional>

namespace qcai2
{

namespace
{

constexpr int k_default_max_results = 25;
constexpr int k_max_results_cap = 200;

bool set_error(QString *error, const QString &message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

QString error_result(const QString &message)
{
    const QString text = message.trimmed();
    return QStringLiteral("Error: %1")
        .arg(text.isEmpty() == true ? QStringLiteral("request failed") : text);
}

int bounded_max_results(const QJsonObject &args, int default_value = k_default_max_results)
{
    return qBound(1, args.value("max_results").toInt(default_value), k_max_results_cap);
}

std::optional<Utils::FilePath> resolve_file_path(const QJsonObject &args, const QString &work_dir,
                                                 clangd_service_t *service, QString *error)
{
    if (service == nullptr)
    {
        set_error(error, QStringLiteral("clangd service is not available."));
        return std::nullopt;
    }

    const QString rel_path = args.value("path").toString().trimmed();
    if (rel_path.isEmpty() == true)
    {
        const std::optional<clangd_location_t> current = service->current_editor_location(error);
        if (current.has_value() == false)
        {
            return std::nullopt;
        }
        return current->file_path;
    }

    const QString absolute_path = QDir(work_dir).absoluteFilePath(rel_path);
    QFileInfo file_info(absolute_path);
    if (file_info.exists() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("File does not exist: %1").arg(absolute_path);
        }
        return std::nullopt;
    }

    const QString project_root = QDir(work_dir).canonicalPath();
    const QString canonical_path = file_info.canonicalFilePath();
    const QString project_prefix = project_root + QDir::separator();
    if (project_root.isEmpty() == false && canonical_path.startsWith(project_prefix) == false &&
        canonical_path != project_root)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Path is outside the project directory.");
        }
        return std::nullopt;
    }

    return Utils::FilePath::fromString(canonical_path);
}

std::optional<clangd_location_t> resolve_location(const QJsonObject &args, const QString &work_dir,
                                                  clangd_service_t *service, QString *error)
{
    const std::optional<Utils::FilePath> file_path =
        resolve_file_path(args, work_dir, service, error);
    if (file_path.has_value() == false)
    {
        return std::nullopt;
    }

    const bool has_line = args.contains("line");
    const bool has_column = args.contains("column");
    if (has_line != has_column)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Both 'line' and 'column' must be provided together.");
        }
        return std::nullopt;
    }

    if (has_line == true)
    {
        return service->resolve_location(file_path.value(), args.value("line").toInt(0),
                                         args.value("column").toInt(0), error);
    }

    const std::optional<clangd_location_t> current = service->current_editor_location(error);
    if (current.has_value() == true && current->file_path == file_path.value())
    {
        return current;
    }

    if (error != nullptr)
    {
        *error = QStringLiteral("The target file is not the current editor. Provide explicit "
                                "'line' and 'column' arguments.");
    }
    return std::nullopt;
}

QString format_range(const clangd_range_t &range)
{
    if (range.is_valid() == false)
    {
        return QStringLiteral("(unknown)");
    }

    return QStringLiteral("%1:%2-%3:%4")
        .arg(range.start_line)
        .arg(range.start_column)
        .arg(range.end_line)
        .arg(range.end_column);
}

QString format_link(const clangd_link_t &link)
{
    return QStringLiteral("%1:%2:%3")
        .arg(link.file_path.toUrlishString())
        .arg(link.line)
        .arg(link.column);
}

QString format_status(const clangd_status_t &status)
{
    QStringList lines;
    lines.append(QStringLiteral("File: %1").arg(status.file_path));
    lines.append(QStringLiteral("Client state: %1")
                     .arg(status.client_state.isEmpty() ? QStringLiteral("(unknown)")
                                                        : status.client_state));
    lines.append(
        QStringLiteral("Reachable: %1")
            .arg(status.reachable == true ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.append(
        QStringLiteral("Fully indexed: %1")
            .arg(status.fully_indexed == true ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.append(
        QStringLiteral("Open in editor: %1")
            .arg(status.document_open == true ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.append(
        QStringLiteral("clangd version: %1")
            .arg(status.version.isEmpty() ? QStringLiteral("(unknown)") : status.version));
    return lines.join(QLatin1Char('\n'));
}

QString format_symbol_info(const clangd_symbol_info_t &symbol_info)
{
    QStringList lines;
    lines.append(QStringLiteral("Location: %1:%2:%3")
                     .arg(symbol_info.file_path)
                     .arg(symbol_info.line)
                     .arg(symbol_info.column));
    lines.append(QStringLiteral("Symbol: %1")
                     .arg(symbol_info.name.isEmpty() == true ? QStringLiteral("(unnamed)")
                                                             : symbol_info.name));
    if (symbol_info.prefix.isEmpty() == false)
    {
        lines.append(QStringLiteral("Scope: %1").arg(symbol_info.prefix));
    }
    return lines.join(QLatin1Char('\n'));
}

QString format_diagnostic(const Utils::FilePath &file_path, const clangd_diagnostic_t &diagnostic)
{
    QString text = QStringLiteral("%1: %2 [%3:%4]")
                       .arg(diagnostic.severity, diagnostic.message, file_path.toUrlishString(),
                            format_range(diagnostic.range));
    if (diagnostic.source.isEmpty() == false)
    {
        text += QStringLiteral(" {source=%1}").arg(diagnostic.source);
    }
    if (diagnostic.code.isEmpty() == false)
    {
        text += QStringLiteral(" {code=%1}").arg(diagnostic.code);
    }
    return text;
}

QString format_symbol_match(const clangd_symbol_match_t &symbol)
{
    QString text = QStringLiteral("%1 (%2) [%3]")
                       .arg(symbol.name, symbol.kind_name, format_link(symbol.link));
    if (symbol.container_name.isEmpty() == false)
    {
        text += QStringLiteral(" in %1").arg(symbol.container_name);
    }
    return text;
}

QString format_completion_item(const clangd_completion_item_t &item)
{
    QString text = QStringLiteral("%1 (%2)").arg(item.label, item.kind_name);
    if (item.detail.isEmpty() == false)
    {
        text += QStringLiteral(" — %1").arg(item.detail);
    }
    if (item.insert_text.isEmpty() == false && item.insert_text != item.label)
    {
        text += QStringLiteral(" {insert=%1}").arg(item.insert_text);
    }
    return text;
}

QString format_hierarchy_item(const clangd_hierarchy_item_t &item)
{
    QString text =
        QStringLiteral("%1 (%2) [%3]").arg(item.name, item.kind_name, format_link(item.link));
    if (item.detail.isEmpty() == false)
    {
        text += QStringLiteral(" — %1").arg(item.detail);
    }
    return text;
}

void append_symbol_outline_lines(const QList<clangd_document_symbol_t> &symbols, int depth,
                                 QStringList *lines)
{
    if (lines == nullptr)
    {
        return;
    }

    for (const clangd_document_symbol_t &symbol : symbols)
    {
        lines->append(QStringLiteral("%1- %2 (%3) [%4]")
                          .arg(QString(depth * 2, QLatin1Char(' ')), symbol.name, symbol.kind_name,
                               format_range(symbol.selection_range.is_valid() == true
                                                ? symbol.selection_range
                                                : symbol.range)));
        append_symbol_outline_lines(symbol.children, depth + 1, lines);
    }
}

void flatten_symbols(const QList<clangd_document_symbol_t> &symbols,
                     QList<const clangd_document_symbol_t *> *flat)
{
    if (flat == nullptr)
    {
        return;
    }

    for (const clangd_document_symbol_t &symbol : symbols)
    {
        flat->append(&symbol);
        flatten_symbols(symbol.children, flat);
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

bool is_local_like(const clangd_document_symbol_t &symbol)
{
    return symbol.kind_name == QStringLiteral("variable") ||
           symbol.kind_name == QStringLiteral("constant") ||
           symbol.kind_name == QStringLiteral("field") ||
           symbol.kind_name == QStringLiteral("property") ||
           symbol.kind_name == QStringLiteral("enum_member");
}

void collect_descendants_if(const clangd_document_symbol_t &root,
                            const std::function<bool(const clangd_document_symbol_t &)> &predicate,
                            QList<const clangd_document_symbol_t *> *results)
{
    if (results == nullptr)
    {
        return;
    }

    for (const clangd_document_symbol_t &child : root.children)
    {
        if (predicate(child) == true)
        {
            results->append(&child);
        }
        collect_descendants_if(child, predicate, results);
    }
}

QString nearest_container_text(const QList<const clangd_document_symbol_t *> &chain)
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

bool completion_is_member_like(const clangd_completion_item_t &item)
{
    return item.kind_name == QStringLiteral("method") ||
           item.kind_name == QStringLiteral("field") ||
           item.kind_name == QStringLiteral("property") ||
           item.kind_name == QStringLiteral("constructor");
}

bool completion_is_scope_api_like(const clangd_completion_item_t &item)
{
    return completion_is_member_like(item) == true ||
           item.kind_name == QStringLiteral("function") ||
           item.kind_name == QStringLiteral("class");
}

bool line_looks_like_write(const QString &symbol_name, const QString &line_text)
{
    if (symbol_name.isEmpty() == true || line_text.isEmpty() == true)
    {
        return false;
    }

    const QString escaped = QRegularExpression::escape(symbol_name);
    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("\\b%1\\b\\s*(?:[+\\-*/%%&|^]?=)").arg(escaped)),
        QRegularExpression(QStringLiteral("(?:\\+\\+|--)\\s*\\b%1\\b").arg(escaped)),
        QRegularExpression(QStringLiteral("\\b%1\\b\\s*(?:\\+\\+|--)").arg(escaped)),
    };

    for (const QRegularExpression &pattern : patterns)
    {
        if (pattern.match(line_text).hasMatch() == true)
        {
            return true;
        }
    }
    return false;
}

QString format_diagnostics_block(const Utils::FilePath &file_path,
                                 const QList<clangd_diagnostic_t> &diagnostics)
{
    if (diagnostics.isEmpty() == true)
    {
        return QStringLiteral("No clangd diagnostics.");
    }

    QStringList lines;
    lines.reserve(diagnostics.size());
    for (const clangd_diagnostic_t &diagnostic : diagnostics)
    {
        lines.append(format_diagnostic(file_path, diagnostic));
    }
    return lines.join(QLatin1Char('\n'));
}

QString build_symbol_signature_text(clangd_service_t *service, const clangd_location_t &location,
                                    const QList<const clangd_document_symbol_t *> &chain)
{
    QString error;
    const QString hover_text = service->hover_text_at(location, &error).trimmed();
    if (hover_text.isEmpty() == false)
    {
        return hover_text;
    }

    if (chain.isEmpty() == false)
    {
        const clangd_document_symbol_t *symbol = chain.last();
        QString text = symbol->name;
        if (symbol->detail.isEmpty() == false)
        {
            text += QStringLiteral(" — %1").arg(symbol->detail);
        }
        return text;
    }

    return QStringLiteral("(signature unavailable)");
}

QString build_symbol_context_pack(clangd_service_t *service, const clangd_location_t &location)
{
    QStringList sections;

    const clangd_symbol_info_t symbol_info = service->symbol_info_at(location);
    if (symbol_info.success == true)
    {
        sections.append(QStringLiteral("Symbol\n%1").arg(format_symbol_info(symbol_info)));
    }

    QString error;
    const QList<clangd_document_symbol_t> symbols =
        service->document_symbols(location.file_path, &error);
    const QList<const clangd_document_symbol_t *> chain = find_symbol_chain(symbols, location);
    if (chain.isEmpty() == false)
    {
        QStringList container_lines;
        for (const clangd_document_symbol_t *symbol : chain)
        {
            container_lines.append(
                QStringLiteral("- %1 (%2)").arg(symbol->name, symbol->kind_name));
        }
        sections.append(
            QStringLiteral("Container chain\n%1").arg(container_lines.join(QLatin1Char('\n'))));
        sections.append(QStringLiteral("Signature\n%1")
                            .arg(build_symbol_signature_text(service, location, chain)));
    }

    const QList<clangd_diagnostic_t> diagnostics = service->diagnostics_at(location, &error);
    if (diagnostics.isEmpty() == false)
    {
        sections.append(QStringLiteral("Diagnostics\n%1")
                            .arg(format_diagnostics_block(location.file_path, diagnostics)));
    }

    const QList<clangd_link_t> references = service->find_references(location, false, 10, &error);
    if (references.isEmpty() == false)
    {
        QStringList ref_lines;
        for (const clangd_link_t &reference : references)
        {
            ref_lines.append(QStringLiteral("- %1").arg(format_link(reference)));
        }
        sections.append(QStringLiteral("References\n%1").arg(ref_lines.join(QLatin1Char('\n'))));
    }

    return sections.join(QStringLiteral("\n\n"));
}

QString build_edit_context_pack(clangd_service_t *service, const clangd_location_t &location)
{
    QStringList sections;
    QString error;

    sections.append(
        QStringLiteral("Code snippet\n%1")
            .arg(service->source_snippet(location.file_path, qMax(1, location.line - 4),
                                         location.line + 4, &error)));

    const QString symbol_context = build_symbol_context_pack(service, location);
    if (symbol_context.isEmpty() == false)
    {
        sections.append(symbol_context);
    }

    const QList<clangd_completion_item_t> completions =
        service->completions_at(location, 15, &error);
    if (completions.isEmpty() == false)
    {
        QStringList lines;
        for (const clangd_completion_item_t &completion : completions)
        {
            lines.append(QStringLiteral("- %1").arg(format_completion_item(completion)));
        }
        sections.append(
            QStringLiteral("Nearby completions\n%1").arg(lines.join(QLatin1Char('\n'))));
    }

    const QList<clangd_diagnostic_t> file_diagnostics =
        service->file_diagnostics(location.file_path, 20000, &error);
    if (file_diagnostics.isEmpty() == false)
    {
        sections.append(QStringLiteral("File diagnostics\n%1")
                            .arg(format_diagnostics_block(location.file_path, file_diagnostics)));
    }

    return sections.join(QStringLiteral("\n\n"));
}

QString build_change_impact_pack(clangd_service_t *service, const clangd_location_t &location)
{
    QStringList sections;
    QString error;

    const QList<clangd_link_t> references = service->find_references(location, false, 20, &error);
    if (references.isEmpty() == false)
    {
        QStringList lines;
        for (const clangd_link_t &reference : references)
        {
            lines.append(QStringLiteral("- reference: %1").arg(format_link(reference)));
        }
        sections.append(QStringLiteral("References\n%1").arg(lines.join(QLatin1Char('\n'))));
    }

    const QList<clangd_link_t> implementations =
        service->find_implementations(location, 20, &error);
    if (implementations.isEmpty() == false)
    {
        QStringList lines;
        for (const clangd_link_t &item : implementations)
        {
            lines.append(QStringLiteral("- implementation: %1").arg(format_link(item)));
        }
        sections.append(
            QStringLiteral("Implementations / overrides\n%1").arg(lines.join(QLatin1Char('\n'))));
    }

    const QList<clangd_hierarchy_item_t> incoming = service->incoming_calls(location, 20, &error);
    if (incoming.isEmpty() == false)
    {
        QStringList lines;
        for (const clangd_hierarchy_item_t &item : incoming)
        {
            lines.append(QStringLiteral("- incoming: %1").arg(format_hierarchy_item(item)));
        }
        sections.append(QStringLiteral("Incoming calls\n%1").arg(lines.join(QLatin1Char('\n'))));
    }

    const QList<clangd_hierarchy_item_t> outgoing = service->outgoing_calls(location, 20, &error);
    if (outgoing.isEmpty() == false)
    {
        QStringList lines;
        for (const clangd_hierarchy_item_t &item : outgoing)
        {
            lines.append(QStringLiteral("- outgoing: %1").arg(format_hierarchy_item(item)));
        }
        sections.append(QStringLiteral("Outgoing calls\n%1").arg(lines.join(QLatin1Char('\n'))));
    }

    return sections.join(QStringLiteral("\n\n"));
}

QString find_executable(const QStringList &names)
{
    for (const QString &name : names)
    {
        const QString path = QStandardPaths::findExecutable(name);
        if (path.isEmpty() == false)
        {
            return path;
        }
    }
    return {};
}

}  // namespace

QString clangd_query_tool_t::name() const
{
    return clangd_query_tool_name(this->kind);
}

QString clangd_query_tool_t::description() const
{
    return clangd_query_tool_description(this->kind);
}

QJsonObject clangd_query_tool_t::args_schema() const
{
    return clangd_query_tool_args_schema(this->kind);
}

QString clangd_query_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    if (this->service == nullptr)
    {
        return error_result(QStringLiteral("clangd service is not available."));
    }

    QString error;

    switch (this->kind)
    {
        case clangd_query_kind_t::STATUS: {
            const QString rel_path = args.value("path").toString().trimmed();
            if (rel_path.isEmpty() == true)
            {
                const clangd_status_t status = this->service->status_for_current_editor(&error);
                return status.available == true ? format_status(status) : error_result(error);
            }

            const std::optional<Utils::FilePath> file_path =
                resolve_file_path(args, work_dir, this->service, &error);
            if (file_path.has_value() == false)
            {
                return error_result(error);
            }

            const clangd_status_t status = this->service->status_for_file(file_path.value());
            return status.available == true ? format_status(status) : error_result(status.error);
        }

        case clangd_query_kind_t::SYMBOL_INFO: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const clangd_symbol_info_t symbol_info =
                this->service->symbol_info_at(location.value());
            return symbol_info.success == true ? format_symbol_info(symbol_info)
                                               : error_result(symbol_info.error);
        }

        case clangd_query_kind_t::DIAGNOSTICS_AT: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_diagnostic_t> diagnostics =
                this->service->diagnostics_at(location.value(), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            if (diagnostics.isEmpty() == true)
            {
                return QStringLiteral("No clangd diagnostics at %1.")
                    .arg(format_link({location->file_path, location->line, location->column, {}}));
            }
            return format_diagnostics_block(location->file_path, diagnostics);
        }

        case clangd_query_kind_t::FOLLOW_SYMBOL: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const std::optional<clangd_link_t> target =
                this->service->follow_symbol(location.value(), &error);
            return target.has_value() == true
                       ? QStringLiteral("Follow target: %1").arg(format_link(target.value()))
                       : error_result(error);
        }

        case clangd_query_kind_t::FIND_SYMBOL_REFERENCES: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_link_t> references = this->service->find_references(
                location.value(), false, bounded_max_results(args), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            if (references.isEmpty() == true)
            {
                return QStringLiteral("No references found.");
            }

            QStringList lines;
            for (const clangd_link_t &reference : references)
            {
                QString preview_error;
                const QString preview =
                    this->service
                        ->line_text_at(reference.file_path, reference.line, &preview_error)
                        .trimmed();
                lines.append(QStringLiteral("%1 %2").arg(format_link(reference), preview));
            }
            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::FIND_SYMBOL_WRITES: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const clangd_symbol_info_t symbol_info =
                this->service->symbol_info_at(location.value());
            const QList<clangd_link_t> references = this->service->find_references(
                location.value(), false, bounded_max_results(args), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }

            const QList<clangd_reference_t> highlights =
                this->service->document_highlights(location.value(), &error);

            QStringList lines;
            for (const clangd_link_t &reference : references)
            {
                bool is_write = false;
                for (const clangd_reference_t &highlight : highlights)
                {
                    if (highlight.kind_name == QStringLiteral("write") &&
                        highlight.link.file_path == reference.file_path &&
                        highlight.link.line == reference.line)
                    {
                        is_write = true;
                        break;
                    }
                }

                QString preview_error;
                const QString preview = this->service->line_text_at(
                    reference.file_path, reference.line, &preview_error);
                if (is_write == false &&
                    line_looks_like_write(symbol_info.name, preview.trimmed()) == true)
                {
                    is_write = true;
                }

                if (is_write == true)
                {
                    lines.append(QStringLiteral("%1 [write] %2")
                                     .arg(format_link(reference), preview.trimmed()));
                }
            }

            return lines.isEmpty() == true ? QStringLiteral("No likely writes found.")
                                           : lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::FIND_OVERRIDES: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_link_t> items = this->service->find_implementations(
                location.value(), bounded_max_results(args), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            if (items.isEmpty() == true)
            {
                return QStringLiteral("No override / implementation targets found.");
            }

            QStringList lines;
            for (const clangd_link_t &item : items)
            {
                lines.append(format_link(item));
            }
            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::OPEN_RELATED_SYMBOL: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const std::optional<clangd_link_t> target =
                this->service->switch_decl_def(location.value(), &error);
            if (target.has_value() == false)
            {
                return error_result(error);
            }

            const Utils::Link link(target->file_path, target->line, target->column);
            QMetaObject::invokeMethod(
                Core::EditorManager::instance(),
                [link]() { Core::EditorManager::openEditorAt(link); }, Qt::QueuedConnection);
            return QStringLiteral("Opened related symbol: %1").arg(format_link(target.value()));
        }

        case clangd_query_kind_t::SEARCH_SYMBOLS: {
            const QString query = args.value("query").toString().trimmed();
            if (query.isEmpty() == true)
            {
                return error_result(QStringLiteral("'query' is required."));
            }

            const QList<clangd_symbol_match_t> symbols =
                this->service->search_symbols(query, bounded_max_results(args), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            if (symbols.isEmpty() == true)
            {
                return QStringLiteral("No symbols found matching '%1'.").arg(query);
            }

            QStringList lines;
            for (const clangd_symbol_match_t &symbol : symbols)
            {
                lines.append(format_symbol_match(symbol));
            }
            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::LIST_FILE_SYMBOLS:
        case clangd_query_kind_t::GET_SYMBOL_OUTLINE: {
            const std::optional<Utils::FilePath> file_path =
                resolve_file_path(args, work_dir, this->service, &error);
            if (file_path.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(file_path.value(), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            if (symbols.isEmpty() == true)
            {
                return QStringLiteral("No file symbols found.");
            }

            if (this->kind == clangd_query_kind_t::GET_SYMBOL_OUTLINE)
            {
                QStringList lines;
                append_symbol_outline_lines(symbols, 0, &lines);
                return lines.join(QLatin1Char('\n'));
            }

            QList<const clangd_document_symbol_t *> flat;
            flatten_symbols(symbols, &flat);
            QStringList lines;
            for (const clangd_document_symbol_t *symbol : flat)
            {
                lines.append(QStringLiteral("%1 (%2) [%3]")
                                 .arg(symbol->name, symbol->kind_name,
                                      format_range(symbol->selection_range.is_valid() == true
                                                       ? symbol->selection_range
                                                       : symbol->range)));
            }
            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::DESCRIBE_SYMBOL_CONTAINER: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(location->file_path, &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }

            const QList<const clangd_document_symbol_t *> chain =
                find_symbol_chain(symbols, location.value());
            if (chain.isEmpty() == true)
            {
                return QStringLiteral("No enclosing symbol container found.");
            }

            const clangd_document_symbol_t *container =
                chain.size() >= 2 ? chain.at(chain.size() - 2) : chain.first();
            QStringList lines;
            lines.append(
                QStringLiteral("Container: %1 (%2)").arg(container->name, container->kind_name));
            lines.append(QStringLiteral("Namespace/class chain: %1")
                             .arg(nearest_container_text(chain).isEmpty() == true
                                      ? QStringLiteral("(none)")
                                      : nearest_container_text(chain)));

            if (container->children.isEmpty() == false)
            {
                lines.append(QStringLiteral("Child symbols:"));
                for (const clangd_document_symbol_t &child : container->children)
                {
                    lines.append(QStringLiteral("- %1 (%2)").arg(child.name, child.kind_name));
                }
            }

            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::GET_FILE_DIAGNOSTICS: {
            const std::optional<Utils::FilePath> file_path =
                resolve_file_path(args, work_dir, this->service, &error);
            if (file_path.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_diagnostic_t> diagnostics =
                this->service->file_diagnostics(file_path.value(), 40000, &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            return format_diagnostics_block(file_path.value(), diagnostics);
        }

        case clangd_query_kind_t::EXPLAIN_DIAGNOSTIC: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_diagnostic_t> diagnostics =
                this->service->diagnostics_at(location.value(), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }
            if (diagnostics.isEmpty() == true)
            {
                return QStringLiteral("No clangd diagnostic at this location.");
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(location->file_path, &error);
            const QList<const clangd_document_symbol_t *> chain =
                find_symbol_chain(symbols, location.value());

            QStringList sections;
            for (const clangd_diagnostic_t &diagnostic : diagnostics)
            {
                QStringList lines;
                lines.append(QStringLiteral("Severity: %1").arg(diagnostic.severity));
                lines.append(QStringLiteral("Message: %1").arg(diagnostic.message));
                if (diagnostic.source.isEmpty() == false)
                {
                    lines.append(QStringLiteral("Source: %1").arg(diagnostic.source));
                }
                if (diagnostic.code.isEmpty() == false)
                {
                    lines.append(QStringLiteral("Code: %1").arg(diagnostic.code));
                }
                lines.append(QStringLiteral("Range: %1").arg(format_range(diagnostic.range)));
                if (chain.isEmpty() == false)
                {
                    lines.append(QStringLiteral("Container: %1").arg(chain.last()->name));
                }
                lines.append(QStringLiteral("Code:\n%1")
                                 .arg(this->service->source_snippet(
                                     location->file_path, qMax(1, diagnostic.range.start_line - 2),
                                     diagnostic.range.end_line + 2, &error)));
                sections.append(lines.join(QLatin1Char('\n')));
            }

            return sections.join(QStringLiteral("\n\n---\n\n"));
        }

        case clangd_query_kind_t::RUN_CLANG_TIDY_ON_FILE: {
            const std::optional<Utils::FilePath> file_path =
                resolve_file_path(args, work_dir, this->service, &error);
            if (file_path.has_value() == false)
            {
                return error_result(error);
            }

            const QString clang_tidy =
                find_executable({QStringLiteral("clang-tidy"), QStringLiteral("clang-tidy-18"),
                                 QStringLiteral("clang-tidy-17")});
            if (clang_tidy.isEmpty() == true)
            {
                return error_result(QStringLiteral("clang-tidy is not installed."));
            }

            QString build_dir = this->service->active_build_dir_for_file(file_path.value());
            if (build_dir.isEmpty() == true)
            {
                build_dir = work_dir;
            }

            process_runner_t runner;
            const auto result =
                runner.run(clang_tidy,
                           {QStringLiteral("-p"), build_dir, QStringLiteral("--quiet"),
                            file_path->toFSPathString()},
                           work_dir, 120000);

            QString output = result.std_out;
            if (result.std_err.isEmpty() == false)
            {
                if (output.isEmpty() == false)
                {
                    output += QStringLiteral("\n");
                }
                output += result.std_err;
            }

            if (output.trimmed().isEmpty() == true)
            {
                output = QStringLiteral("clang-tidy produced no output.");
            }

            if (result.success == false)
            {
                output.prepend(
                    QStringLiteral("clang-tidy exited with code %1.\n").arg(result.exit_code));
            }

            return output;
        }

        case clangd_query_kind_t::FIND_ALL_USES_OF_BROKEN_SYMBOL: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            QStringList sections;
            const QList<clangd_diagnostic_t> diagnostics =
                this->service->diagnostics_at(location.value(), &error);
            if (diagnostics.isEmpty() == false)
            {
                sections.append(
                    QStringLiteral("Diagnostics\n%1")
                        .arg(format_diagnostics_block(location->file_path, diagnostics)));
            }

            const QList<clangd_link_t> references = this->service->find_references(
                location.value(), false, bounded_max_results(args), &error);
            if (references.isEmpty() == false)
            {
                QStringList lines;
                for (const clangd_link_t &reference : references)
                {
                    lines.append(QStringLiteral("- %1").arg(format_link(reference)));
                }
                sections.append(
                    QStringLiteral("References\n%1").arg(lines.join(QLatin1Char('\n'))));
            }

            return sections.isEmpty() == true ? QStringLiteral("No broken-symbol uses found.")
                                              : sections.join(QStringLiteral("\n\n"));
        }

        case clangd_query_kind_t::GET_SEMANTIC_CONTEXT_AT_CURSOR: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            QStringList sections;
            const clangd_symbol_info_t symbol_info =
                this->service->symbol_info_at(location.value());
            if (symbol_info.success == true)
            {
                sections.append(
                    QStringLiteral("Symbol info\n%1").arg(format_symbol_info(symbol_info)));
            }

            const QString hover_text = this->service->hover_text_at(location.value(), &error);
            if (hover_text.isEmpty() == false)
            {
                sections.append(QStringLiteral("Hover\n%1").arg(hover_text));
            }

            const QList<clangd_ast_path_entry_t> ast_path =
                this->service->ast_path_at(location.value(), &error);
            if (ast_path.isEmpty() == false)
            {
                QStringList lines;
                for (const clangd_ast_path_entry_t &entry : ast_path)
                {
                    lines.append(QStringLiteral("- %1 / %2 detail=%3 type=%4 range=%5")
                                     .arg(entry.role, entry.kind,
                                          entry.detail.isEmpty() == true ? QStringLiteral("(none)")
                                                                         : entry.detail,
                                          entry.type_name.isEmpty() == true
                                              ? QStringLiteral("(none)")
                                              : entry.type_name,
                                          format_range(entry.range)));
                }
                sections.append(QStringLiteral("AST path\n%1").arg(lines.join(QLatin1Char('\n'))));
            }

            const QList<clangd_diagnostic_t> diagnostics =
                this->service->diagnostics_at(location.value(), &error);
            if (diagnostics.isEmpty() == false)
            {
                sections.append(
                    QStringLiteral("Diagnostics\n%1")
                        .arg(format_diagnostics_block(location->file_path, diagnostics)));
            }

            return sections.join(QStringLiteral("\n\n"));
        }

        case clangd_query_kind_t::SUMMARIZE_FUNCTION_STRUCTURE: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(location->file_path, &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }

            const QList<const clangd_document_symbol_t *> chain =
                find_symbol_chain(symbols, location.value());
            const clangd_document_symbol_t *function_symbol = nullptr;
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                if (*it != nullptr && is_function_like(**it) == true)
                {
                    function_symbol = *it;
                    break;
                }
            }
            if (function_symbol == nullptr)
            {
                return QStringLiteral("No enclosing function or method found.");
            }

            QStringList sections;
            sections.append(QStringLiteral("Function: %1 (%2)")
                                .arg(function_symbol->name, function_symbol->kind_name));
            sections.append(QStringLiteral("Range: %1").arg(format_range(function_symbol->range)));
            if (function_symbol->detail.isEmpty() == false)
            {
                sections.append(QStringLiteral("Detail: %1").arg(function_symbol->detail));
            }
            sections.append(
                QStringLiteral("Signature:\n%1")
                    .arg(build_symbol_signature_text(this->service, location.value(), chain)));

            QList<const clangd_document_symbol_t *> locals;
            collect_descendants_if(*function_symbol, is_local_like, &locals);
            if (locals.isEmpty() == false)
            {
                QStringList local_lines;
                for (const clangd_document_symbol_t *local : locals)
                {
                    local_lines.append(
                        QStringLiteral("- %1 (%2)").arg(local->name, local->kind_name));
                }
                sections.append(
                    QStringLiteral("Locals\n%1").arg(local_lines.join(QLatin1Char('\n'))));
            }

            const QList<clangd_hierarchy_item_t> outgoing =
                this->service->outgoing_calls(location.value(), 20, &error);
            if (outgoing.isEmpty() == false)
            {
                QStringList call_lines;
                for (const clangd_hierarchy_item_t &call : outgoing)
                {
                    call_lines.append(QStringLiteral("- %1").arg(format_hierarchy_item(call)));
                }
                sections.append(
                    QStringLiteral("Outgoing calls\n%1").arg(call_lines.join(QLatin1Char('\n'))));
            }

            return sections.join(QStringLiteral("\n\n"));
        }

        case clangd_query_kind_t::LIST_LOCALS_IN_SCOPE: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(location->file_path, &error);
            const QList<const clangd_document_symbol_t *> chain =
                find_symbol_chain(symbols, location.value());
            const clangd_document_symbol_t *function_symbol = nullptr;
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                if (*it != nullptr && is_function_like(**it) == true)
                {
                    function_symbol = *it;
                    break;
                }
            }
            if (function_symbol == nullptr)
            {
                return QStringLiteral("No enclosing function or method found.");
            }

            QList<const clangd_document_symbol_t *> locals;
            collect_descendants_if(*function_symbol, is_local_like, &locals);
            if (locals.isEmpty() == true)
            {
                return QStringLiteral("No local-like symbols found in scope.");
            }

            QStringList lines;
            for (const clangd_document_symbol_t *local : locals)
            {
                lines.append(QStringLiteral("%1 (%2) [%3]")
                                 .arg(local->name, local->kind_name,
                                      format_range(local->selection_range.is_valid() == true
                                                       ? local->selection_range
                                                       : local->range)));
            }
            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::GET_SYMBOL_SIGNATURE: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(location->file_path, &error);
            const QList<const clangd_document_symbol_t *> chain =
                find_symbol_chain(symbols, location.value());
            return build_symbol_signature_text(this->service, location.value(), chain);
        }

        case clangd_query_kind_t::GET_ENCLOSING_CLASS_AND_NAMESPACE: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(location->file_path, &error);
            const QList<const clangd_document_symbol_t *> chain =
                find_symbol_chain(symbols, location.value());

            QStringList lines;
            for (const clangd_document_symbol_t *symbol : chain)
            {
                if (symbol != nullptr && is_namespace_or_class_like(*symbol) == true)
                {
                    lines.append(QStringLiteral("%1 (%2)").arg(symbol->name, symbol->kind_name));
                }
            }
            return lines.isEmpty() == true ? QStringLiteral("No enclosing class or namespace.")
                                           : lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::GET_COMPLETIONS_AT:
        case clangd_query_kind_t::GET_MEMBER_CANDIDATES:
        case clangd_query_kind_t::SUGGEST_API_USAGE_IN_SCOPE: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            QList<clangd_completion_item_t> completions =
                this->service->completions_at(location.value(), bounded_max_results(args), &error);
            if (error.isEmpty() == false)
            {
                return error_result(error);
            }

            if (this->kind == clangd_query_kind_t::GET_MEMBER_CANDIDATES)
            {
                QList<clangd_completion_item_t> filtered;
                for (const clangd_completion_item_t &item : completions)
                {
                    if (completion_is_member_like(item) == true)
                    {
                        filtered.append(item);
                    }
                }
                completions = filtered;
            }
            else if (this->kind == clangd_query_kind_t::SUGGEST_API_USAGE_IN_SCOPE)
            {
                QList<clangd_completion_item_t> filtered;
                for (const clangd_completion_item_t &item : completions)
                {
                    if (completion_is_scope_api_like(item) == true)
                    {
                        filtered.append(item);
                    }
                }
                completions = filtered;
            }

            if (completions.isEmpty() == true)
            {
                return QStringLiteral("No completion candidates found.");
            }

            QStringList lines;
            for (const clangd_completion_item_t &item : completions)
            {
                lines.append(format_completion_item(item));
            }
            return lines.join(QLatin1Char('\n'));
        }

        case clangd_query_kind_t::VALIDATE_SYMBOL_AFTER_EDIT: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            if (location.has_value() == false)
            {
                return error_result(error);
            }

            QStringList sections;
            const clangd_symbol_info_t symbol_info =
                this->service->symbol_info_at(location.value());
            if (symbol_info.success == true)
            {
                sections.append(
                    QStringLiteral("Symbol info\n%1").arg(format_symbol_info(symbol_info)));
            }
            else
            {
                sections.append(QStringLiteral("Symbol info\n%1").arg(symbol_info.error));
            }

            const std::optional<clangd_link_t> target =
                this->service->follow_symbol(location.value(), &error);
            sections.append(target.has_value() == true
                                ? QStringLiteral("Follow symbol\nResolved to %1")
                                      .arg(format_link(target.value()))
                                : QStringLiteral("Follow symbol\n%1").arg(error));
            error.clear();

            const QList<clangd_diagnostic_t> diagnostics =
                this->service->diagnostics_at(location.value(), &error);
            sections.append(QStringLiteral("Diagnostics\n%1")
                                .arg(format_diagnostics_block(location->file_path, diagnostics)));
            return sections.join(QStringLiteral("\n\n"));
        }

        case clangd_query_kind_t::GET_SYMBOL_CONTEXT_PACK: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            return location.has_value() == true
                       ? build_symbol_context_pack(this->service, location.value())
                       : error_result(error);
        }

        case clangd_query_kind_t::GET_EDIT_CONTEXT_PACK: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            return location.has_value() == true
                       ? build_edit_context_pack(this->service, location.value())
                       : error_result(error);
        }

        case clangd_query_kind_t::GET_CHANGE_IMPACT_CANDIDATES: {
            const std::optional<clangd_location_t> location =
                resolve_location(args, work_dir, this->service, &error);
            return location.has_value() == true
                       ? build_change_impact_pack(this->service, location.value())
                       : error_result(error);
        }

        case clangd_query_kind_t::ASSEMBLE_PATCH_REVIEW_CONTEXT: {
            const std::optional<Utils::FilePath> file_path =
                resolve_file_path(args, work_dir, this->service, &error);
            if (file_path.has_value() == false)
            {
                return error_result(error);
            }

            QStringList sections;
            const QList<clangd_diagnostic_t> diagnostics =
                this->service->file_diagnostics(file_path.value(), 40000, &error);
            sections.append(QStringLiteral("File diagnostics\n%1")
                                .arg(format_diagnostics_block(file_path.value(), diagnostics)));

            const QList<clangd_document_symbol_t> symbols =
                this->service->document_symbols(file_path.value(), &error);
            QStringList outline_lines;
            append_symbol_outline_lines(symbols, 0, &outline_lines);
            sections.append(
                QStringLiteral("File outline\n%1").arg(outline_lines.join(QLatin1Char('\n'))));

            const bool has_line = args.contains("line");
            const bool has_column = args.contains("column");
            if (has_line == true && has_column == true)
            {
                const std::optional<clangd_location_t> location =
                    resolve_location(args, work_dir, this->service, &error);
                if (location.has_value() == true)
                {
                    sections.append(
                        QStringLiteral("Edit context\n%1")
                            .arg(build_edit_context_pack(this->service, location.value())));
                }
            }

            return sections.join(QStringLiteral("\n\n"));
        }
    }

    return error_result(QStringLiteral("Unsupported clangd tool kind."));
}

void register_clangd_query_tools(tool_registry_t *tool_registry, clangd_service_t *service)
{
    if (tool_registry == nullptr)
    {
        return;
    }

    for (const clangd_query_kind_t kind : clangd_query_tool_kinds())
    {
        tool_registry->register_tool(std::make_shared<clangd_query_tool_t>(kind, service));
    }
}

}  // namespace qcai2
