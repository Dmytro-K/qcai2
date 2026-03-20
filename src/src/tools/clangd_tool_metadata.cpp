#include "clangd_tools.h"

namespace qcai2
{

QList<clangd_query_kind_t> clangd_query_tool_kinds()
{
    return {
        clangd_query_kind_t::STATUS,
        clangd_query_kind_t::SYMBOL_INFO,
        clangd_query_kind_t::DIAGNOSTICS_AT,
        clangd_query_kind_t::FOLLOW_SYMBOL,
        clangd_query_kind_t::FIND_SYMBOL_REFERENCES,
        clangd_query_kind_t::FIND_SYMBOL_WRITES,
        clangd_query_kind_t::FIND_OVERRIDES,
        clangd_query_kind_t::OPEN_RELATED_SYMBOL,
        clangd_query_kind_t::SEARCH_SYMBOLS,
        clangd_query_kind_t::LIST_FILE_SYMBOLS,
        clangd_query_kind_t::GET_SYMBOL_OUTLINE,
        clangd_query_kind_t::DESCRIBE_SYMBOL_CONTAINER,
        clangd_query_kind_t::GET_FILE_DIAGNOSTICS,
        clangd_query_kind_t::EXPLAIN_DIAGNOSTIC,
        clangd_query_kind_t::RUN_CLANG_TIDY_ON_FILE,
        clangd_query_kind_t::FIND_ALL_USES_OF_BROKEN_SYMBOL,
        clangd_query_kind_t::GET_SEMANTIC_CONTEXT_AT_CURSOR,
        clangd_query_kind_t::SUMMARIZE_FUNCTION_STRUCTURE,
        clangd_query_kind_t::LIST_LOCALS_IN_SCOPE,
        clangd_query_kind_t::GET_SYMBOL_SIGNATURE,
        clangd_query_kind_t::GET_ENCLOSING_CLASS_AND_NAMESPACE,
        clangd_query_kind_t::GET_COMPLETIONS_AT,
        clangd_query_kind_t::GET_MEMBER_CANDIDATES,
        clangd_query_kind_t::VALIDATE_SYMBOL_AFTER_EDIT,
        clangd_query_kind_t::SUGGEST_API_USAGE_IN_SCOPE,
        clangd_query_kind_t::GET_SYMBOL_CONTEXT_PACK,
        clangd_query_kind_t::GET_EDIT_CONTEXT_PACK,
        clangd_query_kind_t::GET_CHANGE_IMPACT_CANDIDATES,
        clangd_query_kind_t::ASSEMBLE_PATCH_REVIEW_CONTEXT,
    };
}

QString clangd_query_tool_name(clangd_query_kind_t kind)
{
    switch (kind)
    {
        case clangd_query_kind_t::STATUS:
            return QStringLiteral("clangd_status");
        case clangd_query_kind_t::SYMBOL_INFO:
            return QStringLiteral("clangd_symbol_info");
        case clangd_query_kind_t::DIAGNOSTICS_AT:
            return QStringLiteral("clangd_diagnostics_at");
        case clangd_query_kind_t::FOLLOW_SYMBOL:
            return QStringLiteral("follow_symbol");
        case clangd_query_kind_t::FIND_SYMBOL_REFERENCES:
            return QStringLiteral("find_symbol_references");
        case clangd_query_kind_t::FIND_SYMBOL_WRITES:
            return QStringLiteral("find_symbol_writes");
        case clangd_query_kind_t::FIND_OVERRIDES:
            return QStringLiteral("find_overrides");
        case clangd_query_kind_t::OPEN_RELATED_SYMBOL:
            return QStringLiteral("open_related_symbol");
        case clangd_query_kind_t::SEARCH_SYMBOLS:
            return QStringLiteral("search_symbols");
        case clangd_query_kind_t::LIST_FILE_SYMBOLS:
            return QStringLiteral("list_file_symbols");
        case clangd_query_kind_t::GET_SYMBOL_OUTLINE:
            return QStringLiteral("get_symbol_outline");
        case clangd_query_kind_t::DESCRIBE_SYMBOL_CONTAINER:
            return QStringLiteral("describe_symbol_container");
        case clangd_query_kind_t::GET_FILE_DIAGNOSTICS:
            return QStringLiteral("get_file_diagnostics");
        case clangd_query_kind_t::EXPLAIN_DIAGNOSTIC:
            return QStringLiteral("explain_diagnostic");
        case clangd_query_kind_t::RUN_CLANG_TIDY_ON_FILE:
            return QStringLiteral("run_clang_tidy_on_file");
        case clangd_query_kind_t::FIND_ALL_USES_OF_BROKEN_SYMBOL:
            return QStringLiteral("find_all_uses_of_broken_symbol");
        case clangd_query_kind_t::GET_SEMANTIC_CONTEXT_AT_CURSOR:
            return QStringLiteral("get_semantic_context_at_cursor");
        case clangd_query_kind_t::SUMMARIZE_FUNCTION_STRUCTURE:
            return QStringLiteral("summarize_function_structure");
        case clangd_query_kind_t::LIST_LOCALS_IN_SCOPE:
            return QStringLiteral("list_locals_in_scope");
        case clangd_query_kind_t::GET_SYMBOL_SIGNATURE:
            return QStringLiteral("get_symbol_signature");
        case clangd_query_kind_t::GET_ENCLOSING_CLASS_AND_NAMESPACE:
            return QStringLiteral("get_enclosing_class_and_namespace");
        case clangd_query_kind_t::GET_COMPLETIONS_AT:
            return QStringLiteral("get_completions_at");
        case clangd_query_kind_t::GET_MEMBER_CANDIDATES:
            return QStringLiteral("get_member_candidates");
        case clangd_query_kind_t::VALIDATE_SYMBOL_AFTER_EDIT:
            return QStringLiteral("validate_symbol_after_edit");
        case clangd_query_kind_t::SUGGEST_API_USAGE_IN_SCOPE:
            return QStringLiteral("suggest_api_usage_in_scope");
        case clangd_query_kind_t::GET_SYMBOL_CONTEXT_PACK:
            return QStringLiteral("get_symbol_context_pack");
        case clangd_query_kind_t::GET_EDIT_CONTEXT_PACK:
            return QStringLiteral("get_edit_context_pack");
        case clangd_query_kind_t::GET_CHANGE_IMPACT_CANDIDATES:
            return QStringLiteral("get_change_impact_candidates");
        case clangd_query_kind_t::ASSEMBLE_PATCH_REVIEW_CONTEXT:
            return QStringLiteral("assemble_patch_review_context");
    }

    return QStringLiteral("clangd_unknown");
}

QString clangd_query_tool_description(clangd_query_kind_t kind)
{
    switch (kind)
    {
        case clangd_query_kind_t::STATUS:
            return QStringLiteral(
                "Show ClangCodeModel/clangd status for a file or the current editor. "
                "Args: path (optional, project-relative file path).");
        case clangd_query_kind_t::SYMBOL_INFO:
            return QStringLiteral("Ask clangd for symbol information at a position. "
                                  "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::DIAGNOSTICS_AT:
            return QStringLiteral("Show clangd diagnostics that apply at a position. "
                                  "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::FOLLOW_SYMBOL:
            return QStringLiteral("Resolve the symbol under the cursor to its definition target. "
                                  "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::FIND_SYMBOL_REFERENCES:
            return QStringLiteral("Find clangd references for the symbol at a position. "
                                  "Args: path (optional), line (optional), column (optional), "
                                  "max_results (optional).");
        case clangd_query_kind_t::FIND_SYMBOL_WRITES:
            return QStringLiteral("Find likely write locations for the symbol at a position. "
                                  "Args: path (optional), line (optional), column (optional), "
                                  "max_results (optional).");
        case clangd_query_kind_t::FIND_OVERRIDES:
            return QStringLiteral(
                "Find override/implementation targets for the symbol at a position. "
                "Args: path (optional), line (optional), column (optional), "
                "max_results (optional).");
        case clangd_query_kind_t::OPEN_RELATED_SYMBOL:
            return QStringLiteral(
                "Open the related declaration/definition for the symbol at a position "
                "in the editor. Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::SEARCH_SYMBOLS:
            return QStringLiteral("Search workspace symbols through clangd. "
                                  "Args: query (required), max_results (optional).");
        case clangd_query_kind_t::LIST_FILE_SYMBOLS:
            return QStringLiteral(
                "List file symbols in flat form for a file or the current editor. "
                "Args: path (optional).");
        case clangd_query_kind_t::GET_SYMBOL_OUTLINE:
            return QStringLiteral("Show the clangd outline tree for a file or the current editor. "
                                  "Args: path (optional).");
        case clangd_query_kind_t::DESCRIBE_SYMBOL_CONTAINER:
            return QStringLiteral(
                "Describe the enclosing symbol container and nearby child symbols. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_FILE_DIAGNOSTICS:
            return QStringLiteral("Collect clangd diagnostics for an entire file. "
                                  "Args: path (optional).");
        case clangd_query_kind_t::EXPLAIN_DIAGNOSTIC:
            return QStringLiteral(
                "Explain the clangd diagnostic at a position with source context. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::RUN_CLANG_TIDY_ON_FILE:
            return QStringLiteral(
                "Run clang-tidy on a project file using the active build directory. "
                "Args: path (optional).");
        case clangd_query_kind_t::FIND_ALL_USES_OF_BROKEN_SYMBOL:
            return QStringLiteral("Find references for a symbol that currently has diagnostics. "
                                  "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_SEMANTIC_CONTEXT_AT_CURSOR:
            return QStringLiteral(
                "Collect semantic context at a position: symbol info, hover text, AST "
                "path, and diagnostics. Args: path (optional), line (optional), "
                "column (optional).");
        case clangd_query_kind_t::SUMMARIZE_FUNCTION_STRUCTURE:
            return QStringLiteral(
                "Summarize the enclosing function/method structure, locals, and calls. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::LIST_LOCALS_IN_SCOPE:
            return QStringLiteral("List local-like symbols inside the enclosing function scope. "
                                  "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_SYMBOL_SIGNATURE:
            return QStringLiteral(
                "Get the best available signature/hover text for the symbol at a position. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_ENCLOSING_CLASS_AND_NAMESPACE:
            return QStringLiteral(
                "Report the enclosing class/struct/interface and namespace chain. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_COMPLETIONS_AT:
            return QStringLiteral("Query clangd completions at a position. "
                                  "Args: path (optional), line (optional), column (optional), "
                                  "max_results (optional).");
        case clangd_query_kind_t::GET_MEMBER_CANDIDATES:
            return QStringLiteral("Get member-like completion candidates at a position. "
                                  "Args: path (optional), line (optional), column (optional), "
                                  "max_results (optional).");
        case clangd_query_kind_t::VALIDATE_SYMBOL_AFTER_EDIT:
            return QStringLiteral(
                "Validate a recently edited symbol using follow-symbol and diagnostics. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::SUGGEST_API_USAGE_IN_SCOPE:
            return QStringLiteral(
                "Suggest relevant API candidates in the current scope from clangd "
                "completions. Args: path (optional), line (optional), column (optional), "
                "max_results (optional).");
        case clangd_query_kind_t::GET_SYMBOL_CONTEXT_PACK:
            return QStringLiteral(
                "Assemble a structured context pack for the symbol at a position. "
                "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_EDIT_CONTEXT_PACK:
            return QStringLiteral("Assemble a structured edit context pack around a position. "
                                  "Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::GET_CHANGE_IMPACT_CANDIDATES:
            return QStringLiteral(
                "Collect references, implementations, and call hierarchy as change-impact "
                "candidates. Args: path (optional), line (optional), column (optional).");
        case clangd_query_kind_t::ASSEMBLE_PATCH_REVIEW_CONTEXT:
            return QStringLiteral(
                "Assemble file diagnostics, outline, and edit context for reviewing a patch. "
                "Args: path (optional), line (optional), column (optional).");
    }

    return {};
}

QJsonObject clangd_query_tool_args_schema(clangd_query_kind_t kind)
{
    QJsonObject schema{
        {"path",
         QJsonObject{{"type", "string"}, {"description", "Optional project-relative file path"}}},
        {"line", QJsonObject{{"type", "integer"}, {"description", "Optional 1-based line"}}},
        {"column", QJsonObject{{"type", "integer"}, {"description", "Optional 1-based column"}}},
        {"max_results",
         QJsonObject{{"type", "integer"}, {"description", "Optional result limit"}}},
        {"query",
         QJsonObject{{"type", "string"}, {"description", "Optional query for search-like tools"}}},
    };

    if (kind == clangd_query_kind_t::SEARCH_SYMBOLS)
    {
        schema.insert("query", QJsonObject{{"type", "string"}, {"required", true}});
    }

    return schema;
}

}  // namespace qcai2
