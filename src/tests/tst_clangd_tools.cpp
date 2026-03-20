/*! Unit tests for clangd-backed tool registration. */

#include <QtTest>

#include <QJsonArray>
#include <QSet>
#include <QStringList>

#include "src/tools/clangd_tools.h"

using namespace qcai2;

namespace
{

QStringList expected_clangd_tool_names()
{
    return {
        QStringLiteral("clangd_status"),
        QStringLiteral("clangd_symbol_info"),
        QStringLiteral("clangd_diagnostics_at"),
        QStringLiteral("follow_symbol"),
        QStringLiteral("find_symbol_references"),
        QStringLiteral("find_symbol_writes"),
        QStringLiteral("find_overrides"),
        QStringLiteral("open_related_symbol"),
        QStringLiteral("search_symbols"),
        QStringLiteral("list_file_symbols"),
        QStringLiteral("get_symbol_outline"),
        QStringLiteral("describe_symbol_container"),
        QStringLiteral("get_file_diagnostics"),
        QStringLiteral("explain_diagnostic"),
        QStringLiteral("run_clang_tidy_on_file"),
        QStringLiteral("find_all_uses_of_broken_symbol"),
        QStringLiteral("get_semantic_context_at_cursor"),
        QStringLiteral("summarize_function_structure"),
        QStringLiteral("list_locals_in_scope"),
        QStringLiteral("get_symbol_signature"),
        QStringLiteral("get_enclosing_class_and_namespace"),
        QStringLiteral("get_completions_at"),
        QStringLiteral("get_member_candidates"),
        QStringLiteral("validate_symbol_after_edit"),
        QStringLiteral("suggest_api_usage_in_scope"),
        QStringLiteral("get_symbol_context_pack"),
        QStringLiteral("get_edit_context_pack"),
        QStringLiteral("get_change_impact_candidates"),
        QStringLiteral("assemble_patch_review_context"),
    };
}

}  // namespace

class clangd_tools_test_t : public QObject
{
    Q_OBJECT

private slots:
    void clangd_query_tool_kinds_expose_expected_surface();
    void search_symbols_schema_requires_query();
};

void clangd_tools_test_t::clangd_query_tool_kinds_expose_expected_surface()
{
    QStringList expected_names = expected_clangd_tool_names();
    QStringList actual_names;
    for (const clangd_query_kind_t kind : clangd_query_tool_kinds())
    {
        actual_names.append(clangd_query_tool_name(kind));
        QVERIFY(!clangd_query_tool_description(kind).isEmpty());
    }
    expected_names.sort();
    actual_names.sort();

    QCOMPARE(actual_names, expected_names);
    QCOMPARE(QSet<QString>(actual_names.begin(), actual_names.end()).size(),
             expected_names.size());
}

void clangd_tools_test_t::search_symbols_schema_requires_query()
{
    const QJsonObject schema = clangd_query_tool_args_schema(clangd_query_kind_t::SEARCH_SYMBOLS);
    QVERIFY(schema.contains(QStringLiteral("query")));
    QCOMPARE(schema.value(QStringLiteral("query"))
                 .toObject()
                 .value(QStringLiteral("required"))
                 .toBool(),
             true);
}

QTEST_APPLESS_MAIN(clangd_tools_test_t)

#include "tst_clangd_tools.moc"
