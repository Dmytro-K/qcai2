/*! Implements reusable ClangCodeModel/clangd queries used by tools and future features. */
#include "clangd_service.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cursorineditor.h>
#include <languageclient/client.h>
#include <languageclient/languageclientmanager.h>
#include <languageserverprotocol/callhierarchy.h>
#include <languageserverprotocol/completion.h>
#include <languageserverprotocol/languagefeatures.h>
#include <languageserverprotocol/workspace.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <utils/link.h>
#include <utils/searchresultitem.h>

#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QScopeGuard>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <memory>
#include <variant>

namespace qcai2
{

namespace
{

constexpr int default_timeout_ms = 1500;

bool set_error(QString *error, const QString &message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

QString symbol_kind_name(int kind)
{
    using LanguageServerProtocol::SymbolKind;

    switch (static_cast<SymbolKind>(kind))
    {
        case SymbolKind::File:
            return QStringLiteral("file");
        case SymbolKind::Module:
            return QStringLiteral("module");
        case SymbolKind::Namespace:
            return QStringLiteral("namespace");
        case SymbolKind::Package:
            return QStringLiteral("package");
        case SymbolKind::Class:
            return QStringLiteral("class");
        case SymbolKind::Method:
            return QStringLiteral("method");
        case SymbolKind::Property:
            return QStringLiteral("property");
        case SymbolKind::Field:
            return QStringLiteral("field");
        case SymbolKind::Constructor:
            return QStringLiteral("constructor");
        case SymbolKind::Enum:
            return QStringLiteral("enum");
        case SymbolKind::Interface:
            return QStringLiteral("interface");
        case SymbolKind::Function:
            return QStringLiteral("function");
        case SymbolKind::Variable:
            return QStringLiteral("variable");
        case SymbolKind::Constant:
            return QStringLiteral("constant");
        case SymbolKind::String:
            return QStringLiteral("string");
        case SymbolKind::Number:
            return QStringLiteral("number");
        case SymbolKind::Boolean:
            return QStringLiteral("boolean");
        case SymbolKind::Array:
            return QStringLiteral("array");
        case SymbolKind::Object:
            return QStringLiteral("object");
        case SymbolKind::Key:
            return QStringLiteral("key");
        case SymbolKind::Null:
            return QStringLiteral("null");
        case SymbolKind::EnumMember:
            return QStringLiteral("enum_member");
        case SymbolKind::Struct:
            return QStringLiteral("struct");
        case SymbolKind::Event:
            return QStringLiteral("event");
        case SymbolKind::Operator:
            return QStringLiteral("operator");
        case SymbolKind::TypeParameter:
            return QStringLiteral("type_parameter");
    }

    return QStringLiteral("unknown");
}

QString completion_kind_name(int kind)
{
    using namespace LanguageServerProtocol::CompletionItemKind;

    switch (kind)
    {
        case Text:
            return QStringLiteral("text");
        case Method:
            return QStringLiteral("method");
        case Function:
            return QStringLiteral("function");
        case Constructor:
            return QStringLiteral("constructor");
        case Field:
            return QStringLiteral("field");
        case Variable:
            return QStringLiteral("variable");
        case Class:
            return QStringLiteral("class");
        case Interface:
            return QStringLiteral("interface");
        case Module:
            return QStringLiteral("module");
        case Property:
            return QStringLiteral("property");
        case Unit:
            return QStringLiteral("unit");
        case Value:
            return QStringLiteral("value");
        case Enum:
            return QStringLiteral("enum");
        case Keyword:
            return QStringLiteral("keyword");
        case Snippet:
            return QStringLiteral("snippet");
        case Color:
            return QStringLiteral("color");
        case File:
            return QStringLiteral("file");
        case Reference:
            return QStringLiteral("reference");
        case Folder:
            return QStringLiteral("folder");
        case EnumMember:
            return QStringLiteral("enum_member");
        case Constant:
            return QStringLiteral("constant");
        case Struct:
            return QStringLiteral("struct");
        case Event:
            return QStringLiteral("event");
        case Operator:
            return QStringLiteral("operator");
        case TypeParameter:
            return QStringLiteral("type_parameter");
    }

    return QStringLiteral("unknown");
}

QString highlight_kind_name(int kind)
{
    switch (kind)
    {
        case LanguageServerProtocol::DocumentHighlight::Read:
            return QStringLiteral("read");
        case LanguageServerProtocol::DocumentHighlight::Write:
            return QStringLiteral("write");
        case LanguageServerProtocol::DocumentHighlight::Text:
            return QStringLiteral("text");
    }

    return QStringLiteral("unknown");
}

QString diagnostic_severity_to_string(
    const std::optional<LanguageServerProtocol::DiagnosticSeverity> &severity)
{
    if (severity.has_value() == false)
    {
        return QStringLiteral("unknown");
    }

    switch (severity.value())
    {
        case LanguageServerProtocol::DiagnosticSeverity::Error:
            return QStringLiteral("error");
        case LanguageServerProtocol::DiagnosticSeverity::Warning:
            return QStringLiteral("warning");
        case LanguageServerProtocol::DiagnosticSeverity::Information:
            return QStringLiteral("information");
        case LanguageServerProtocol::DiagnosticSeverity::Hint:
            return QStringLiteral("hint");
    }

    return QStringLiteral("unknown");
}

QString
diagnostic_code_to_string(const std::optional<LanguageServerProtocol::Diagnostic::Code> &code)
{
    if (code.has_value() == false)
    {
        return {};
    }

    if (std::holds_alternative<int>(code.value()) == true)
    {
        return QString::number(std::get<int>(code.value()));
    }

    return std::get<QString>(code.value());
}

clangd_range_t range_from_lsp(const LanguageServerProtocol::Range &range)
{
    clangd_range_t converted;
    converted.start_line = range.start().line() + 1;
    converted.start_column = range.start().character() + 1;
    converted.end_line = range.end().line() + 1;
    converted.end_column = range.end().character() + 1;
    return converted;
}

clangd_link_t link_from_utils_link(const Utils::Link &link, const QString &title = {})
{
    clangd_link_t converted;
    converted.file_path = link.targetFilePath;
    converted.line = link.target.line;
    converted.column = link.target.column;
    converted.title = title;
    return converted;
}

clangd_link_t link_from_location(LanguageClient::Client *client,
                                 const LanguageServerProtocol::Location &location,
                                 const QString &title = {})
{
    return link_from_utils_link(location.toLink(client->hostPathMapper()), title);
}

LanguageClient::Client *client_for_file_path(const Utils::FilePath &file_path)
{
    if (file_path.isEmpty() == true)
    {
        return nullptr;
    }

    return LanguageClient::LanguageClientManager::clientForFilePath(file_path);
}

LanguageClient::Client *workspace_client()
{
    if (Core::IEditor *editor = Core::EditorManager::currentEditor(); editor != nullptr)
    {
        if (auto *document = qobject_cast<TextEditor::TextDocument *>(editor->document());
            document != nullptr)
        {
            if (auto *client = LanguageClient::LanguageClientManager::clientForDocument(document);
                client != nullptr && client->reachable() == true)
            {
                return client;
            }
        }
    }

    const QList<LanguageClient::Client *> clients =
        LanguageClient::LanguageClientManager::clients();
    for (LanguageClient::Client *client : clients)
    {
        if (client != nullptr && client->reachable() == true)
        {
            return client;
        }
    }
    return nullptr;
}

TextEditor::TextDocument *text_document_for_file(const Utils::FilePath &file_path,
                                                 LanguageClient::Client *client)
{
    if (client != nullptr)
    {
        if (auto *document = client->documentForFilePath(file_path); document != nullptr)
        {
            return document;
        }
    }

    if (Core::IDocument *document = Core::DocumentModel::documentForFilePath(file_path);
        document != nullptr)
    {
        return qobject_cast<TextEditor::TextDocument *>(document);
    }

    return TextEditor::TextDocument::textDocumentForFilePath(file_path);
}

QString file_text(const Utils::FilePath &file_path, QString *error)
{
    const QMap<Utils::FilePath, QString> opened_contents =
        TextEditor::TextDocument::openedTextDocumentContents();
    if (opened_contents.contains(file_path) == true)
    {
        return opened_contents.value(file_path);
    }

    if (TextEditor::TextDocument *document =
            TextEditor::TextDocument::textDocumentForFilePath(file_path);
        document != nullptr)
    {
        return document->plainText();
    }

    QFile file(file_path.toFSPathString());
    if (file.open(QIODevice::ReadOnly) == false)
    {
        set_error(error, QStringLiteral("Failed to open file: %1").arg(file_path.toUserOutput()));
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QTextDocument *snapshot_document(const Utils::FilePath &file_path, QString *error,
                                 std::unique_ptr<QTextDocument> *owned_document)
{
    if (owned_document == nullptr)
    {
        set_error(error, QStringLiteral("Internal error: owned document target is null."));
        return nullptr;
    }

    if (TextEditor::TextDocument *document =
            TextEditor::TextDocument::textDocumentForFilePath(file_path);
        document != nullptr && document->document() != nullptr)
    {
        return document->document();
    }

    const QString text = file_text(file_path, error);
    if (text.isNull() == true)
    {
        return nullptr;
    }

    owned_document->reset(new QTextDocument(text));
    return owned_document->get();
}

LanguageServerProtocol::TextDocumentIdentifier
identifier_for_file(LanguageClient::Client *client, const Utils::FilePath &file_path)
{
    return LanguageServerProtocol::TextDocumentIdentifier(client->hostPathToServerUri(file_path));
}

LanguageServerProtocol::TextDocumentPositionParams
params_for_location(LanguageClient::Client *client, const clangd_location_t &location)
{
    return LanguageServerProtocol::TextDocumentPositionParams(
        identifier_for_file(client, location.file_path), location.position);
}

QString marked_string_to_text(const LanguageServerProtocol::MarkedString &value)
{
    if (const auto *text = std::get_if<QString>(&value); text != nullptr)
    {
        return *text;
    }

    if (const auto *code = std::get_if<LanguageServerProtocol::MarkedLanguageString>(&value);
        code != nullptr)
    {
        return code->value();
    }

    return {};
}

QString hover_content_to_text(const LanguageServerProtocol::HoverContent &content)
{
    if (const auto *marked = std::get_if<LanguageServerProtocol::MarkedString>(&content);
        marked != nullptr)
    {
        return marked_string_to_text(*marked);
    }

    if (const auto *marked_list =
            std::get_if<QList<LanguageServerProtocol::MarkedString>>(&content);
        marked_list != nullptr)
    {
        QStringList lines;
        lines.reserve(marked_list->size());
        for (const LanguageServerProtocol::MarkedString &item : *marked_list)
        {
            const QString text = marked_string_to_text(item).trimmed();
            if (text.isEmpty() == false)
            {
                lines.append(text);
            }
        }
        return lines.join(QStringLiteral("\n\n"));
    }

    if (const auto *markup = std::get_if<LanguageServerProtocol::MarkupContent>(&content);
        markup != nullptr)
    {
        return markup->content();
    }

    return {};
}

QString
markup_or_string_to_text(const std::optional<LanguageServerProtocol::MarkupOrString> &value)
{
    if (value.has_value() == false)
    {
        return {};
    }

    if (const auto *text = std::get_if<QString>(&value.value()); text != nullptr)
    {
        return *text;
    }

    if (const auto *markup = std::get_if<LanguageServerProtocol::MarkupContent>(&value.value());
        markup != nullptr)
    {
        return markup->content();
    }

    return {};
}

template <typename RequestType>
std::optional<typename RequestType::Response>
run_request(LanguageClient::Client *client, const typename RequestType::Parameters &params,
            QString *error, int timeout_ms = default_timeout_ms)
{
    if (client == nullptr)
    {
        set_error(error, QStringLiteral("ClangCodeModel client is not available."));
        return std::nullopt;
    }
    if (client->reachable() == false)
    {
        set_error(error, QStringLiteral("The clangd client is not reachable."));
        return std::nullopt;
    }
    if (params.isValid() == false)
    {
        set_error(error, QStringLiteral("Invalid LSP request parameters."));
        return std::nullopt;
    }

    struct request_state_t
    {
        std::optional<typename RequestType::Response> response;
        bool received = false;
    };

    const auto state = std::make_shared<request_state_t>();

    QEventLoop loop;
    QPointer<QEventLoop> loop_guard(&loop);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client, &QObject::destroyed, &loop, &QEventLoop::quit);

    RequestType request(params);
    request.setResponseCallback([state, loop_guard](const typename RequestType::Response &reply) {
        state->response = reply;
        state->received = true;
        if (loop_guard.isNull() == false)
        {
            loop_guard->quit();
        }
    });
    client->sendMessage(request);

    timer.start(qMax(1, timeout_ms));
    loop.exec();

    if (state->received == false || state->response.has_value() == false)
    {
        set_error(error, QStringLiteral("Timed out while waiting for clangd response."));
        return std::nullopt;
    }

    if (state->response->error().has_value() == true)
    {
        set_error(error, state->response->error()->toString());
        return std::nullopt;
    }

    return state->response;
}

QList<clangd_link_t> links_from_goto_result(LanguageClient::Client *client,
                                            const LanguageServerProtocol::GotoResult &result,
                                            int max_results)
{
    QList<clangd_link_t> links;
    if (const auto *single = std::get_if<LanguageServerProtocol::Location>(&result);
        single != nullptr)
    {
        links.append(link_from_location(client, *single));
        return links;
    }

    if (const auto *many = std::get_if<QList<LanguageServerProtocol::Location>>(&result);
        many != nullptr)
    {
        const int count = qMin(max_results, static_cast<int>(many->size()));
        links.reserve(count);
        for (int index = 0; index < count; ++index)
        {
            links.append(link_from_location(client, many->at(index)));
        }
    }

    return links;
}

clangd_document_symbol_t
convert_document_symbol(const LanguageServerProtocol::DocumentSymbol &symbol)
{
    clangd_document_symbol_t converted;
    converted.name = symbol.name();
    converted.detail = symbol.detail().value_or(QString());
    converted.kind = symbol.kind();
    converted.kind_name = symbol_kind_name(symbol.kind());
    converted.range = range_from_lsp(symbol.range());
    converted.selection_range = range_from_lsp(symbol.selectionRange());

    const std::optional<QList<LanguageServerProtocol::DocumentSymbol>> children =
        symbol.children();
    if (children.has_value() == true)
    {
        converted.children.reserve(children->size());
        for (const LanguageServerProtocol::DocumentSymbol &child : children.value())
        {
            converted.children.append(convert_document_symbol(child));
        }
    }

    return converted;
}

clangd_document_symbol_t
convert_symbol_information(LanguageClient::Client *client,
                           const LanguageServerProtocol::SymbolInformation &symbol)
{
    clangd_document_symbol_t converted;
    converted.name = symbol.name();
    converted.detail = symbol.containerName().value_or(QString());
    converted.kind = symbol.kind();
    converted.kind_name = symbol_kind_name(symbol.kind());
    const clangd_link_t link = link_from_location(client, symbol.location());
    converted.range.start_line = link.line;
    converted.range.start_column = link.column;
    converted.range.end_line = link.line;
    converted.range.end_column = qMax(link.column, 1);
    converted.selection_range = converted.range;
    return converted;
}

clangd_completion_item_t
convert_completion_item(const LanguageServerProtocol::CompletionItem &item)
{
    clangd_completion_item_t converted;
    converted.label = item.label();
    converted.detail = item.detail().value_or(QString());
    converted.documentation = markup_or_string_to_text(item.documentation());
    converted.insert_text = item.textEdit().has_value() == true
                                ? item.textEdit()->newText()
                                : item.insertText().value_or(item.label());
    converted.kind = item.kind().value_or(0);
    converted.kind_name = completion_kind_name(converted.kind);
    return converted;
}

clangd_reference_t convert_highlight(const clangd_location_t &location, QTextDocument *document,
                                     const LanguageServerProtocol::DocumentHighlight &highlight)
{
    const clangd_range_t range = range_from_lsp(highlight.range());
    clangd_reference_t converted;
    converted.kind = highlight.kind().value_or(0);
    converted.kind_name = highlight_kind_name(converted.kind);
    converted.link.file_path = location.file_path;
    converted.link.line = range.start_line;
    converted.link.column = range.start_column;
    converted.preview = document->findBlockByNumber(range.start_line - 1).text().trimmed();
    return converted;
}

clangd_hierarchy_item_t
convert_call_item(const LanguageServerProtocol::CallHierarchyItem &item,
                  const QList<LanguageServerProtocol::Range> &source_ranges)
{
    clangd_hierarchy_item_t converted;
    converted.name = item.name();
    converted.detail = item.detail().value_or(QString());
    converted.kind = static_cast<int>(item.symbolKind());
    converted.kind_name = symbol_kind_name(converted.kind);
    converted.link.file_path = item.uri().toFilePath({});
    converted.link.line = item.selectionRange().start().line() + 1;
    converted.link.column = item.selectionRange().start().character() + 1;

    converted.source_ranges.reserve(source_ranges.size());
    for (const LanguageServerProtocol::Range &range : source_ranges)
    {
        converted.source_ranges.append(range_from_lsp(range));
    }

    return converted;
}

clangd_range_t preferred_symbol_range(const clangd_document_symbol_t &symbol)
{
    return symbol.selection_range.is_valid() == true ? symbol.selection_range : symbol.range;
}

bool append_symbol_path(const QList<clangd_document_symbol_t> &symbols, int line, int column,
                        QList<clangd_document_symbol_t> *path)
{
    if (path == nullptr)
    {
        return false;
    }

    for (const clangd_document_symbol_t &symbol : symbols)
    {
        const clangd_range_t range = preferred_symbol_range(symbol);
        if (range.contains(line, column) == false)
        {
            continue;
        }

        path->append(symbol);
        append_symbol_path(symbol.children, line, column, path);
        return true;
    }

    return false;
}

clangd_symbol_info_t symbol_info_from_path(const QList<clangd_document_symbol_t> &path,
                                           const clangd_location_t &location)
{
    clangd_symbol_info_t result;
    result.file_path = location.file_path.toUrlishString();
    result.line = location.line;
    result.column = location.column;

    if (path.isEmpty() == true)
    {
        result.error = QStringLiteral("No enclosing document symbol was found at the requested "
                                      "position.");
        return result;
    }

    result.name = path.last().name;
    QStringList prefix;
    for (int index = 0; index + 1 < path.size(); ++index)
    {
        if (path.at(index).name.isEmpty() == false)
        {
            prefix.append(path.at(index).name);
        }
    }
    result.prefix = prefix.join(QStringLiteral("::"));
    result.success = result.name.isEmpty() == false || result.prefix.isEmpty() == false;
    if (result.success == false)
    {
        result.error =
            QStringLiteral("No symbol metadata is available at the requested position.");
    }
    return result;
}

QList<clangd_ast_path_entry_t> ast_path_from_symbols(const QList<clangd_document_symbol_t> &path)
{
    QList<clangd_ast_path_entry_t> converted;
    converted.reserve(path.size());
    for (const clangd_document_symbol_t &symbol : path)
    {
        clangd_ast_path_entry_t entry;
        entry.role = QStringLiteral("document_symbol");
        entry.kind = symbol.kind_name;
        entry.detail = symbol.name;
        entry.type_name = symbol.detail;
        entry.range = preferred_symbol_range(symbol);
        converted.append(entry);
    }
    return converted;
}

QString first_meaningful_line(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() == false)
        {
            return trimmed;
        }
    }
    return {};
}

QStringList file_lines(const Utils::FilePath &file_path, QString *error)
{
    const QString text = file_text(file_path, error);
    if (text.isNull() == true)
    {
        return {};
    }
    return text.split(QLatin1Char('\n'));
}

}  // namespace

clangd_service_t::clangd_service_t(QObject *parent) : QObject(parent)
{
}

std::optional<clangd_location_t> clangd_service_t::current_editor_location(QString *error) const
{
    Core::IEditor *editor = Core::EditorManager::currentEditor();
    if (editor == nullptr)
    {
        set_error(error, QStringLiteral("No active editor is open."));
        return std::nullopt;
    }

    auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
    if (text_editor == nullptr)
    {
        set_error(error, QStringLiteral("The active editor is not a text editor."));
        return std::nullopt;
    }

    TextEditor::TextDocument *document = text_editor->textDocument();
    if (document == nullptr || document->filePath().isEmpty() == true)
    {
        set_error(error,
                  QStringLiteral("The active editor does not have a file-backed text document."));
        return std::nullopt;
    }

    const QTextCursor cursor = text_editor->textCursor();

    clangd_location_t location;
    location.file_path = document->filePath();
    location.position =
        LanguageServerProtocol::Position(cursor.blockNumber(), cursor.positionInBlock());
    location.line = cursor.blockNumber() + 1;
    location.column = cursor.positionInBlock() + 1;
    return location;
}

std::optional<clangd_location_t>
clangd_service_t::resolve_location(const Utils::FilePath &file_path, int line, int column,
                                   QString *error) const
{
    if (file_path.isEmpty() == true)
    {
        set_error(error, QStringLiteral("Target file path is empty."));
        return std::nullopt;
    }
    if (line <= 0 || column <= 0)
    {
        set_error(error, QStringLiteral("Line and column must be positive 1-based values."));
        return std::nullopt;
    }

    clangd_location_t location;
    location.file_path = file_path;
    location.line = line;
    location.column = column;
    location.position = LanguageServerProtocol::Position(line - 1, column - 1);
    return location;
}

clangd_status_t clangd_service_t::status_for_current_editor(QString *error) const
{
    clangd_status_t status;
    const std::optional<clangd_location_t> location = this->current_editor_location(error);
    if (location.has_value() == false)
    {
        status.error = (error != nullptr) ? *error : QStringLiteral("No active editor is open.");
        return status;
    }

    return this->status_for_file(location->file_path);
}

clangd_status_t clangd_service_t::status_for_file(const Utils::FilePath &file_path) const
{
    clangd_status_t status;
    status.file_path = file_path.toUrlishString();

    if (file_path.isEmpty() == true)
    {
        status.error = QStringLiteral("Target file path is empty.");
        return status;
    }

    auto *client = client_for_file_path(file_path);
    if (client == nullptr)
    {
        status.error =
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file.");
        return status;
    }

    status.available = true;
    status.reachable = client->reachable();
    status.fully_indexed = false;
    status.client_state = client->stateString();
    status.version = client->serverVersion().trimmed();
    status.document_open = client->documentForFilePath(file_path) != nullptr;
    return status;
}

clangd_symbol_info_t clangd_service_t::symbol_info_at(const clangd_location_t &location,
                                                      int timeout_ms) const
{
    Q_UNUSED(timeout_ms);

    if (location.is_valid() == false)
    {
        clangd_symbol_info_t result;
        result.file_path = location.file_path.toUrlishString();
        result.line = location.line;
        result.column = location.column;
        result.error = QStringLiteral("Invalid clangd location.");
        return result;
    }

    QString symbols_error;
    const QList<clangd_document_symbol_t> symbols =
        this->document_symbols(location.file_path, &symbols_error);
    QList<clangd_document_symbol_t> symbol_path;
    append_symbol_path(symbols, location.line, location.column, &symbol_path);

    clangd_symbol_info_t result = symbol_info_from_path(symbol_path, location);
    if (result.success == true)
    {
        return result;
    }

    QString hover_error;
    const QString hover_text = this->hover_text_at(location, &hover_error);
    const QString hover_summary = first_meaningful_line(hover_text);
    if (hover_summary.isEmpty() == false)
    {
        result.name = hover_summary;
        result.prefix.clear();
        result.error.clear();
        result.success = true;
        return result;
    }

    if (symbols_error.isEmpty() == false)
    {
        result.error = symbols_error;
    }
    else if (hover_error.isEmpty() == false)
    {
        result.error = hover_error;
    }
    return result;
}

QList<clangd_diagnostic_t> clangd_service_t::diagnostics_at(const clangd_location_t &location,
                                                            QString *error) const
{
    QList<clangd_diagnostic_t> formatted_diagnostics;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return formatted_diagnostics;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return formatted_diagnostics;
    }

    std::unique_ptr<QTextDocument> owned_document;
    QTextDocument *snapshot = snapshot_document(location.file_path, error, &owned_document);
    if (snapshot == nullptr)
    {
        return formatted_diagnostics;
    }

    const QTextCursor cursor = location.position.toTextCursor(snapshot);
    const QList<LanguageServerProtocol::Diagnostic> diagnostics =
        client->diagnosticsAt(location.file_path, cursor);

    formatted_diagnostics.reserve(diagnostics.size());
    for (const LanguageServerProtocol::Diagnostic &diagnostic : diagnostics)
    {
        clangd_diagnostic_t item;
        item.severity = diagnostic_severity_to_string(diagnostic.severity());
        item.message = diagnostic.message();
        item.source = diagnostic.source().value_or(QString());
        item.code = diagnostic_code_to_string(diagnostic.code());
        item.range = range_from_lsp(diagnostic.range());
        formatted_diagnostics.append(item);
    }

    return formatted_diagnostics;
}

QList<clangd_diagnostic_t> clangd_service_t::file_diagnostics(const Utils::FilePath &file_path,
                                                              int max_scan_chars,
                                                              QString *error) const
{
    QList<clangd_diagnostic_t> collected_diagnostics;

    auto *client = client_for_file_path(file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return collected_diagnostics;
    }

    std::unique_ptr<QTextDocument> owned_document;
    QTextDocument *snapshot = snapshot_document(file_path, error, &owned_document);
    if (snapshot == nullptr)
    {
        return collected_diagnostics;
    }

    QHash<QString, bool> seen;
    const int max_position =
        qMin(qMax(0, max_scan_chars), qMax(0, snapshot->characterCount() - 1));

    for (int position = 0; position <= max_position; ++position)
    {
        QTextCursor cursor(snapshot);
        cursor.setPosition(position);
        const QList<LanguageServerProtocol::Diagnostic> diagnostics =
            client->diagnosticsAt(file_path, cursor);
        for (const LanguageServerProtocol::Diagnostic &diagnostic : diagnostics)
        {
            const clangd_range_t range = range_from_lsp(diagnostic.range());
            const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                                    .arg(diagnostic.message(),
                                         diagnostic_severity_to_string(diagnostic.severity()),
                                         diagnostic.source().value_or(QString()),
                                         diagnostic_code_to_string(diagnostic.code()))
                                    .arg(range.start_line)
                                    .arg(range.start_column)
                                    .arg(range.end_line)
                                    .arg(range.end_column);
            if (seen.contains(key) == true)
            {
                continue;
            }
            seen.insert(key, true);

            clangd_diagnostic_t item;
            item.severity = diagnostic_severity_to_string(diagnostic.severity());
            item.message = diagnostic.message();
            item.source = diagnostic.source().value_or(QString());
            item.code = diagnostic_code_to_string(diagnostic.code());
            item.range = range;
            collected_diagnostics.append(item);
        }
    }

    return collected_diagnostics;
}

std::optional<clangd_link_t> clangd_service_t::follow_symbol(const clangd_location_t &location,
                                                             QString *error) const
{
    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return std::nullopt;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return std::nullopt;
    }

    const auto response = run_request<LanguageServerProtocol::GotoDefinitionRequest>(
        client, params_for_location(client, location), error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return std::nullopt;
    }

    const QList<clangd_link_t> links =
        links_from_goto_result(client, response->result().value(), 1);
    if (links.isEmpty() == true)
    {
        set_error(error, QStringLiteral("clangd did not return a symbol target."));
        return std::nullopt;
    }

    return links.first();
}

std::optional<clangd_link_t> clangd_service_t::switch_decl_def(const clangd_location_t &location,
                                                               QString *error) const
{
    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return std::nullopt;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return std::nullopt;
    }

    TextEditor::TextDocument *document = text_document_for_file(location.file_path, client);
    if (document == nullptr || document->document() == nullptr)
    {
        set_error(
            error,
            QStringLiteral(
                "Declaration/definition switching requires the file to be open in the editor."));
        return std::nullopt;
    }

    const QTextCursor cursor = location.position.toTextCursor(document->document());
    const CppEditor::CursorInEditor cursor_in_editor(cursor, location.file_path, nullptr,
                                                     document);

    struct switch_decl_def_state_t
    {
        bool received = false;
        clangd_link_t result;
    };

    const auto state = std::make_shared<switch_decl_def_state_t>();
    QEventLoop loop;
    QPointer<QEventLoop> loop_guard(&loop);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client, &QObject::destroyed, &loop, &QEventLoop::quit);

    CppEditor::CppModelManager::switchDeclDef(
        cursor_in_editor,
        [state, loop_guard](const Utils::Link &link) {
            if (link.hasValidTarget() == true)
            {
                state->result = link_from_utils_link(link);
            }
            state->received = true;
            if (loop_guard.isNull() == false)
            {
                loop_guard->quit();
            }
        },
        CppEditor::CppModelManager::Backend::Best);

    timer.start(default_timeout_ms);
    loop.exec();

    if (state->received == false || state->result.is_valid() == false)
    {
        set_error(error,
                  QStringLiteral("clangd did not return a related declaration/definition."));
        return std::nullopt;
    }

    return state->result;
}

QList<clangd_link_t> clangd_service_t::find_references(const clangd_location_t &location,
                                                       bool include_declaration, int max_results,
                                                       QString *error) const
{
    QList<clangd_link_t> links;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return links;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return links;
    }

    LanguageServerProtocol::ReferenceParams params(params_for_location(client, location));
    params.setContext(
        LanguageServerProtocol::ReferenceParams::ReferenceContext(include_declaration));

    const auto response =
        run_request<LanguageServerProtocol::FindReferencesRequest>(client, params, error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return links;
    }

    const QList<LanguageServerProtocol::Location> locations = response->result()->toListOrEmpty();
    const int count = qMin(max_results, static_cast<int>(locations.size()));
    links.reserve(count);
    for (int index = 0; index < count; ++index)
    {
        links.append(link_from_location(client, locations.at(index)));
    }

    return links;
}

QList<clangd_link_t> clangd_service_t::find_implementations(const clangd_location_t &location,
                                                            int max_results, QString *error) const
{
    QList<clangd_link_t> links;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return links;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return links;
    }

    const auto response = run_request<LanguageServerProtocol::GotoImplementationRequest>(
        client, params_for_location(client, location), error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return links;
    }

    return links_from_goto_result(client, response->result().value(), max_results);
}

QList<clangd_symbol_match_t>
clangd_service_t::search_symbols(const QString &query, int max_results, QString *error) const
{
    QList<clangd_symbol_match_t> matches;
    if (query.trimmed().isEmpty() == true)
    {
        set_error(error, QStringLiteral("Query is required."));
        return matches;
    }

    LanguageClient::Client *client = workspace_client();
    if (client == nullptr)
    {
        set_error(error, QStringLiteral("No active clangd workspace client is available."));
        return matches;
    }

    LanguageServerProtocol::WorkspaceSymbolParams params;
    params.setQuery(query);
    params.setLimit(qMax(1, max_results));

    const auto response =
        run_request<LanguageServerProtocol::WorkspaceSymbolRequest>(client, params, error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return matches;
    }

    const QList<LanguageServerProtocol::SymbolInformation> symbols =
        response->result()->toListOrEmpty();
    matches.reserve(symbols.size());
    for (const LanguageServerProtocol::SymbolInformation &symbol : symbols)
    {
        clangd_symbol_match_t match;
        match.name = symbol.name();
        match.detail = symbol.containerName().value_or(QString());
        match.container_name = symbol.containerName().value_or(QString());
        match.kind = symbol.kind();
        match.kind_name = symbol_kind_name(symbol.kind());
        match.link = link_from_location(client, symbol.location());
        matches.append(match);
    }

    return matches;
}

QList<clangd_document_symbol_t>
clangd_service_t::document_symbols(const Utils::FilePath &file_path, QString *error) const
{
    QList<clangd_document_symbol_t> symbols;

    auto *client = client_for_file_path(file_path);
    if (client == nullptr)
    {
        client = workspace_client();
    }
    if (client == nullptr)
    {
        set_error(error, QStringLiteral(
                             "ClangCodeModel does not have an active clangd workspace client for "
                             "this file."));
        return symbols;
    }

    const LanguageServerProtocol::DocumentSymbolParams params(
        identifier_for_file(client, file_path));
    const auto response =
        run_request<LanguageServerProtocol::DocumentSymbolsRequest>(client, params, error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return symbols;
    }

    const LanguageServerProtocol::DocumentSymbolsResult result = response->result().value();
    if (const auto *document_symbols =
            std::get_if<QList<LanguageServerProtocol::DocumentSymbol>>(&result);
        document_symbols != nullptr)
    {
        symbols.reserve(document_symbols->size());
        for (const LanguageServerProtocol::DocumentSymbol &symbol : *document_symbols)
        {
            symbols.append(convert_document_symbol(symbol));
        }
        return symbols;
    }

    if (const auto *symbol_information =
            std::get_if<QList<LanguageServerProtocol::SymbolInformation>>(&result);
        symbol_information != nullptr)
    {
        symbols.reserve(symbol_information->size());
        for (const LanguageServerProtocol::SymbolInformation &symbol : *symbol_information)
        {
            symbols.append(convert_symbol_information(client, symbol));
        }
    }

    return symbols;
}

QList<clangd_ast_path_entry_t> clangd_service_t::ast_path_at(const clangd_location_t &location,
                                                             QString *error) const
{
    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return {};
    }

    QString symbols_error;
    const QList<clangd_document_symbol_t> symbols =
        this->document_symbols(location.file_path, &symbols_error);
    QList<clangd_document_symbol_t> symbol_path;
    append_symbol_path(symbols, location.line, location.column, &symbol_path);
    if (symbol_path.isEmpty() == true)
    {
        set_error(error, symbols_error.isEmpty() == true
                             ? QStringLiteral("No enclosing document symbol was found at the "
                                              "requested position.")
                             : symbols_error);
        return {};
    }

    return ast_path_from_symbols(symbol_path);
}

QString clangd_service_t::hover_text_at(const clangd_location_t &location, QString *error) const
{
    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return {};
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return {};
    }

    const auto response = run_request<LanguageServerProtocol::HoverRequest>(
        client, params_for_location(client, location), error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return {};
    }

    const LanguageServerProtocol::HoverResult hover_result = response->result().value();
    if (const auto *hover = std::get_if<LanguageServerProtocol::Hover>(&hover_result);
        hover != nullptr)
    {
        return hover_content_to_text(hover->content()).trimmed();
    }

    return {};
}

QList<clangd_signature_t> clangd_service_t::signature_help_at(const clangd_location_t &location,
                                                              int *active_signature,
                                                              QString *error) const
{
    QList<clangd_signature_t> signatures;

    if (active_signature != nullptr)
    {
        *active_signature = -1;
    }

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return signatures;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return signatures;
    }

    const auto response = run_request<LanguageServerProtocol::SignatureHelpRequest>(
        client, params_for_location(client, location), error);
    if (response.has_value() == false || response->result().has_value() == false ||
        response->result()->isNull() == true)
    {
        return signatures;
    }

    const LanguageServerProtocol::SignatureHelp signature_help = response->result()->value();
    if (active_signature != nullptr)
    {
        *active_signature = signature_help.activeSignature().value_or(-1);
    }

    const QList<LanguageServerProtocol::SignatureInformation> lsp_signatures =
        signature_help.signatures();
    signatures.reserve(lsp_signatures.size());
    for (const LanguageServerProtocol::SignatureInformation &signature : lsp_signatures)
    {
        clangd_signature_t item;
        item.label = signature.label();
        item.documentation = markup_or_string_to_text(signature.documentation());
        item.active_parameter = signature.activeParameter().value_or(-1);

        const std::optional<QList<LanguageServerProtocol::ParameterInformation>> parameters =
            signature.parameters();
        if (parameters.has_value() == true)
        {
            item.parameters.reserve(parameters->size());
            for (const LanguageServerProtocol::ParameterInformation &parameter :
                 parameters.value())
            {
                item.parameters.append(parameter.label());
            }
        }

        signatures.append(item);
    }

    return signatures;
}

QList<clangd_completion_item_t> clangd_service_t::completions_at(const clangd_location_t &location,
                                                                 int max_results,
                                                                 QString *error) const
{
    QList<clangd_completion_item_t> completions;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return completions;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return completions;
    }

    LanguageServerProtocol::CompletionParams params(params_for_location(client, location));
    params.setLimit(qMax(1, max_results));
    LanguageServerProtocol::CompletionParams::CompletionContext context;
    context.setTriggerKind(LanguageServerProtocol::CompletionParams::Invoked);
    params.setContext(context);

    const auto response =
        run_request<LanguageServerProtocol::CompletionRequest>(client, params, error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return completions;
    }

    QList<LanguageServerProtocol::CompletionItem> items;
    const LanguageServerProtocol::CompletionResult result = response->result().value();
    if (const auto *list = std::get_if<QList<LanguageServerProtocol::CompletionItem>>(&result);
        list != nullptr)
    {
        items = *list;
    }
    else if (const auto *completion_list =
                 std::get_if<LanguageServerProtocol::CompletionList>(&result);
             completion_list != nullptr && completion_list->items().has_value() == true)
    {
        items = completion_list->items().value();
    }

    const int count = qMin(max_results, static_cast<int>(items.size()));
    completions.reserve(count);
    for (int index = 0; index < count; ++index)
    {
        completions.append(convert_completion_item(items.at(index)));
    }

    return completions;
}

QList<clangd_reference_t> clangd_service_t::document_highlights(const clangd_location_t &location,
                                                                QString *error) const
{
    QList<clangd_reference_t> highlights;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return highlights;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return highlights;
    }

    std::unique_ptr<QTextDocument> owned_document;
    QTextDocument *snapshot = snapshot_document(location.file_path, error, &owned_document);
    if (snapshot == nullptr)
    {
        return highlights;
    }

    const auto response = run_request<LanguageServerProtocol::DocumentHighlightsRequest>(
        client, params_for_location(client, location), error);
    if (response.has_value() == false || response->result().has_value() == false)
    {
        return highlights;
    }

    const LanguageServerProtocol::DocumentHighlightsResult result = response->result().value();
    if (const auto *items = std::get_if<QList<LanguageServerProtocol::DocumentHighlight>>(&result);
        items != nullptr)
    {
        highlights.reserve(items->size());
        for (const LanguageServerProtocol::DocumentHighlight &item : *items)
        {
            highlights.append(convert_highlight(location, snapshot, item));
        }
    }

    return highlights;
}

QList<clangd_hierarchy_item_t> clangd_service_t::incoming_calls(const clangd_location_t &location,
                                                                int max_results,
                                                                QString *error) const
{
    QList<clangd_hierarchy_item_t> calls;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return calls;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return calls;
    }

    const auto prepare_response = run_request<LanguageServerProtocol::PrepareCallHierarchyRequest>(
        client, params_for_location(client, location), error);
    if (prepare_response.has_value() == false || prepare_response->result().has_value() == false)
    {
        return calls;
    }

    const QList<LanguageServerProtocol::CallHierarchyItem> roots =
        prepare_response->result()->toListOrEmpty();
    for (const LanguageServerProtocol::CallHierarchyItem &root : roots)
    {
        if (calls.size() >= max_results)
        {
            break;
        }

        LanguageServerProtocol::CallHierarchyCallsParams params;
        params.setItem(root);
        const auto response =
            run_request<LanguageServerProtocol::CallHierarchyIncomingCallsRequest>(client, params,
                                                                                   error);
        if (response.has_value() == false || response->result().has_value() == false)
        {
            continue;
        }

        const QList<LanguageServerProtocol::CallHierarchyIncomingCall> items =
            response->result()->toListOrEmpty();
        for (const LanguageServerProtocol::CallHierarchyIncomingCall &item : items)
        {
            if (calls.size() >= max_results)
            {
                break;
            }
            calls.append(convert_call_item(item.from(), item.fromRanges()));
        }
    }

    return calls;
}

QList<clangd_hierarchy_item_t> clangd_service_t::outgoing_calls(const clangd_location_t &location,
                                                                int max_results,
                                                                QString *error) const
{
    QList<clangd_hierarchy_item_t> calls;

    if (location.is_valid() == false)
    {
        set_error(error, QStringLiteral("Invalid clangd location."));
        return calls;
    }

    auto *client = client_for_file_path(location.file_path);
    if (client == nullptr)
    {
        set_error(
            error,
            QStringLiteral("ClangCodeModel does not have an active clangd client for this file."));
        return calls;
    }

    const auto prepare_response = run_request<LanguageServerProtocol::PrepareCallHierarchyRequest>(
        client, params_for_location(client, location), error);
    if (prepare_response.has_value() == false || prepare_response->result().has_value() == false)
    {
        return calls;
    }

    const QList<LanguageServerProtocol::CallHierarchyItem> roots =
        prepare_response->result()->toListOrEmpty();
    for (const LanguageServerProtocol::CallHierarchyItem &root : roots)
    {
        if (calls.size() >= max_results)
        {
            break;
        }

        LanguageServerProtocol::CallHierarchyCallsParams params;
        params.setItem(root);
        const auto response =
            run_request<LanguageServerProtocol::CallHierarchyOutgoingCallsRequest>(client, params,
                                                                                   error);
        if (response.has_value() == false || response->result().has_value() == false)
        {
            continue;
        }

        const QList<LanguageServerProtocol::CallHierarchyOutgoingCall> items =
            response->result()->toListOrEmpty();
        for (const LanguageServerProtocol::CallHierarchyOutgoingCall &item : items)
        {
            if (calls.size() >= max_results)
            {
                break;
            }
            calls.append(convert_call_item(item.to(), item.fromRanges()));
        }
    }

    return calls;
}

QString clangd_service_t::line_text_at(const Utils::FilePath &file_path, int line,
                                       QString *error) const
{
    if (line <= 0)
    {
        set_error(error, QStringLiteral("Line must be a positive 1-based value."));
        return {};
    }

    const QStringList lines = file_lines(file_path, error);
    if (lines.isEmpty() == true)
    {
        return {};
    }
    if (line > lines.size())
    {
        set_error(error, QStringLiteral("Line %1 is outside the file.").arg(line));
        return {};
    }

    return lines.at(line - 1);
}

QString clangd_service_t::source_snippet(const Utils::FilePath &file_path, int start_line,
                                         int end_line, QString *error) const
{
    if (start_line <= 0 || end_line < start_line)
    {
        set_error(error, QStringLiteral("Invalid snippet range."));
        return {};
    }

    const QStringList lines = file_lines(file_path, error);
    if (lines.isEmpty() == true)
    {
        return {};
    }

    const int first = qMax(1, start_line);
    const int last = qMin(end_line, static_cast<int>(lines.size()));
    QStringList snippet_lines;
    snippet_lines.reserve(last - first + 1);
    for (int line = first; line <= last; ++line)
    {
        snippet_lines.append(QStringLiteral("%1: %2").arg(line, 4).arg(lines.at(line - 1)));
    }
    return snippet_lines.join(QLatin1Char('\n'));
}

QString clangd_service_t::active_build_dir_for_file(const Utils::FilePath &file_path) const
{
    const QList<ProjectExplorer::Project *> projects =
        ProjectExplorer::ProjectManager::projectsForFile(file_path);
    for (ProjectExplorer::Project *project : projects)
    {
        if (project == nullptr || project->activeTarget() == nullptr ||
            project->activeTarget()->activeBuildConfiguration() == nullptr)
        {
            continue;
        }

        const Utils::FilePath build_dir =
            project->activeTarget()->activeBuildConfiguration()->buildDirectory();
        if (build_dir.isEmpty() == false)
        {
            return build_dir.toFSPathString();
        }
    }

    return {};
}

}  // namespace qcai2
