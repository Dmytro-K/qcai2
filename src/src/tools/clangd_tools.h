#pragma once

#include "i_tool.h"

#include <QList>

namespace qcai2
{

class clangd_service_t;
class tool_registry_t;

enum class clangd_query_kind_t
{
    STATUS,
    SYMBOL_INFO,
    DIAGNOSTICS_AT,
    FOLLOW_SYMBOL,
    FIND_SYMBOL_REFERENCES,
    FIND_SYMBOL_WRITES,
    FIND_OVERRIDES,
    OPEN_RELATED_SYMBOL,
    SEARCH_SYMBOLS,
    LIST_FILE_SYMBOLS,
    GET_SYMBOL_OUTLINE,
    DESCRIBE_SYMBOL_CONTAINER,
    GET_FILE_DIAGNOSTICS,
    EXPLAIN_DIAGNOSTIC,
    RUN_CLANG_TIDY_ON_FILE,
    FIND_ALL_USES_OF_BROKEN_SYMBOL,
    GET_SEMANTIC_CONTEXT_AT_CURSOR,
    SUMMARIZE_FUNCTION_STRUCTURE,
    LIST_LOCALS_IN_SCOPE,
    GET_SYMBOL_SIGNATURE,
    GET_ENCLOSING_CLASS_AND_NAMESPACE,
    GET_COMPLETIONS_AT,
    GET_MEMBER_CANDIDATES,
    VALIDATE_SYMBOL_AFTER_EDIT,
    SUGGEST_API_USAGE_IN_SCOPE,
    GET_SYMBOL_CONTEXT_PACK,
    GET_EDIT_CONTEXT_PACK,
    GET_CHANGE_IMPACT_CANDIDATES,
    ASSEMBLE_PATCH_REVIEW_CONTEXT,
};

class clangd_query_tool_t : public i_tool_t
{
public:
    clangd_query_tool_t(clangd_query_kind_t kind, clangd_service_t *service)
        : kind(kind), service(service)
    {
    }

    QString name() const override;
    QString description() const override;
    QJsonObject args_schema() const override;
    QString execute(const QJsonObject &args, const QString &work_dir) override;

private:
    const clangd_query_kind_t kind;
    clangd_service_t *const service = nullptr;
};

QList<clangd_query_kind_t> clangd_query_tool_kinds();
QString clangd_query_tool_name(clangd_query_kind_t kind);
QString clangd_query_tool_description(clangd_query_kind_t kind);
QJsonObject clangd_query_tool_args_schema(clangd_query_kind_t kind);
void register_clangd_query_tools(tool_registry_t *tool_registry, clangd_service_t *service);

}  // namespace qcai2
