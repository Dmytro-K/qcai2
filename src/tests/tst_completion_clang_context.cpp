/*! @file
    @brief Regression tests for shared clang-derived completion context helpers.
*/

#include "../src/clangd/clangd_service.h"
#include "../src/completion/completion_clang_context.h"

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <QTextDocument>

namespace qcai2
{

namespace
{

QString build_class_body_section_for_document(const QString &source)
{
    QTextDocument document;
    document.setPlainText(source);

    clangd_range_t range;
    range.start_line = 1;
    range.start_column = 1;
    range.end_line = document.blockCount();
    range.end_column = 1;

    const QString class_body = extract_short_completion_symbol_snippet(&document, range, 80, 4000);
    return class_body.isEmpty() == false
               ? QStringLiteral("Current class body (short)\n~~~~text\n%1\n~~~~").arg(class_body)
               : QString();
}

clangd_document_symbol_t make_symbol(const QString &name, const QString &kind_name, int start_line,
                                     int start_column, int end_line, int end_column,
                                     QList<clangd_document_symbol_t> children = {})
{
    clangd_document_symbol_t symbol;
    symbol.name = name;
    symbol.kind_name = kind_name;
    symbol.range = {start_line, start_column, end_line, end_column};
    symbol.selection_range = symbol.range;
    symbol.children = std::move(children);
    return symbol;
}

clangd_location_t make_location(const QString &file_path, int line, int column)
{
    clangd_location_t location;
    location.file_path = Utils::FilePath::fromString(file_path);
    location.line = line;
    location.column = column;
    location.position = LanguageServerProtocol::Position(line - 1, column - 1);
    return location;
}

}  // namespace

class tst_completion_clang_context_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Leading include extraction should keep top-of-file preamble lines around includes.
     */
    void extract_include_block_returns_leading_include_section();

    /**
     * Include extraction should ignore files that never reach an include block.
     */
    void extract_include_block_returns_empty_without_include();

    /**
     * Short symbol snippets should be rendered with numbered lines.
     */
    void extract_short_symbol_snippet_returns_numbered_text();

    /**
     * Long symbol snippets should be dropped to keep autocomplete prompts bounded.
     */
    void extract_short_symbol_snippet_rejects_long_ranges();

    /**
     * Small class bodies should fit the completion-context limits.
     */
    void small_class_body_snippet_is_available();

    /**
     * Large class bodies should be excluded to keep prompts bounded.
     */
    void large_class_body_snippet_is_rejected();

    /**
     * When the current file only exposes an out-of-class method definition, the related
     * declaration context should still resolve the owning class body.
     */
    void select_completion_class_context_uses_related_declaration_when_needed();

    /**
     * Related declaration lookup should still run even when the method signature already exposes
     * the owning class name.
     */
    void should_resolve_related_class_context_ignores_owner_symbol_availability();

    /**
     * Trace output should explain when clangd service is unavailable.
     */
    void build_completion_clang_context_block_records_trace_without_service();

    /**
     * Workspace-symbol matching should accept fully qualified symbol names.
     */
    void workspace_symbol_matches_owner_accepts_qualified_name();

    /**
     * Workspace-symbol matching should reconstruct qualified names from container metadata.
     */
    void workspace_symbol_matches_owner_accepts_container_qualified_name();

    /**
     * Text fallback should recover a class body when AST-backed document symbols are unavailable.
     */
    void find_class_context_via_text_scan_recovers_class_range();

    /**
     * Qualified method signatures should expose the owning class for workspace lookup.
     */
    void parse_completion_owner_symbol_extracts_qualified_owner();

    /**
     * Unqualified method signatures should still reuse the enclosing namespace scope.
     */
    void parse_completion_owner_symbol_uses_fallback_scope();
};

void tst_completion_clang_context_t::extract_include_block_returns_leading_include_section()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("// License\n"
                                         "#pragma once\n"
                                         "\n"
                                         "#include <vector>\n"
                                         "#include \"widget.h\"\n"
                                         "\n"
                                         "class example_t {};"));

    const QString include_block = extract_completion_include_block(&document);
    QVERIFY(include_block.contains(QStringLiteral("#pragma once")));
    QVERIFY(include_block.contains(QStringLiteral("#include <vector>")));
    QVERIFY(include_block.contains(QStringLiteral("#include \"widget.h\"")));
    QVERIFY(include_block.contains(QStringLiteral("   2: #pragma once")));
}

void tst_completion_clang_context_t::extract_include_block_returns_empty_without_include()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("int value = 0;\n"));

    QCOMPARE(extract_completion_include_block(&document), QString());
}

void tst_completion_clang_context_t::extract_short_symbol_snippet_returns_numbered_text()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("namespace demo {\n"
                                         "void run()\n"
                                         "{\n"
                                         "    work();\n"
                                         "}\n"
                                         "}\n"));

    clangd_range_t range;
    range.start_line = 2;
    range.start_column = 1;
    range.end_line = 5;
    range.end_column = 1;

    QCOMPARE(extract_short_completion_symbol_snippet(&document, range, 6, 400),
             QStringLiteral("   2: void run()\n"
                            "   3: {\n"
                            "   4:     work();\n"
                            "   5: }"));
}

void tst_completion_clang_context_t::extract_short_symbol_snippet_rejects_long_ranges()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("line1\nline2\nline3\nline4\nline5\n"));

    clangd_range_t range;
    range.start_line = 1;
    range.start_column = 1;
    range.end_line = 5;
    range.end_column = 1;

    QCOMPARE(extract_short_completion_symbol_snippet(&document, range, 3, 400), QString());
    QCOMPARE(extract_short_completion_symbol_snippet(&document, range, 10, 10), QString());
}

void tst_completion_clang_context_t::small_class_body_snippet_is_available()
{
    const QString section =
        build_class_body_section_for_document(QStringLiteral("class example_t\n"
                                                             "{\n"
                                                             "public:\n"
                                                             "    void run();\n"
                                                             "    int value = 0;\n"
                                                             "};\n"));

    QVERIFY(section.contains(QStringLiteral("Current class body (short)")));
    QVERIFY(section.contains(QStringLiteral("   1: class example_t")));
    QVERIFY(section.contains(QStringLiteral("   6: };")));
}

void tst_completion_clang_context_t::large_class_body_snippet_is_rejected()
{
    QString source = QStringLiteral("class example_t\n{\n");
    for (int index = 0; index < 120; ++index)
    {
        source += QStringLiteral("    int member_%1 = %1;\n").arg(index);
    }
    source += QStringLiteral("};\n");

    QCOMPARE(build_class_body_section_for_document(source), QString());
}

void tst_completion_clang_context_t::
    select_completion_class_context_uses_related_declaration_when_needed()
{
    const QList<clangd_document_symbol_t> current_symbols = {make_symbol(
        QStringLiteral("qcai2"), QStringLiteral("namespace"), 1, 1, 120, 1,
        {make_symbol(QStringLiteral("complete"), QStringLiteral("method"), 40, 1, 90, 1)})};
    const clangd_location_t current_location =
        make_location(QStringLiteral("/tmp/provider.cpp"), 55, 5);

    const QList<clangd_document_symbol_t> related_symbols = {make_symbol(
        QStringLiteral("qcai2"), QStringLiteral("namespace"), 1, 1, 80, 1,
        {make_symbol(
            QStringLiteral("copilot_provider_t"), QStringLiteral("class"), 5, 1, 30, 1,
            {make_symbol(QStringLiteral("complete"), QStringLiteral("method"), 18, 5, 18, 30)})})};
    const clangd_location_t related_location =
        make_location(QStringLiteral("/tmp/provider.h"), 18, 12);

    const completion_class_context_t context = select_completion_class_context(
        current_symbols, current_location, related_symbols, related_location);

    QVERIFY(context.is_valid());
    QCOMPARE(context.file_path, Utils::FilePath::fromString(QStringLiteral("/tmp/provider.h")));
    QCOMPARE(context.class_name, QStringLiteral("copilot_provider_t"));
    QCOMPARE(context.enclosing_scope, QStringLiteral("qcai2::copilot_provider_t"));
    QCOMPARE(context.class_range.start_line, 5);
    QCOMPARE(context.class_range.end_line, 30);
}

void tst_completion_clang_context_t::
    should_resolve_related_class_context_ignores_owner_symbol_availability()
{
    completion_class_context_t current_context;

    clangd_document_symbol_t function_symbol;
    function_symbol.name = QStringLiteral("complete");
    function_symbol.kind_name = QStringLiteral("method");
    function_symbol.range = {40, 1, 90, 1};
    function_symbol.selection_range = function_symbol.range;

    QVERIFY(should_resolve_related_class_context(current_context, &function_symbol));

    current_context.file_path = Utils::FilePath::fromString(QStringLiteral("/tmp/provider.h"));
    current_context.class_name = QStringLiteral("copilot_provider_t");
    current_context.class_range = {5, 1, 30, 1};

    QVERIFY(should_resolve_related_class_context(current_context, &function_symbol) == false);
    QVERIFY(should_resolve_related_class_context({}, nullptr) == false);
}

void tst_completion_clang_context_t::
    build_completion_clang_context_block_records_trace_without_service()
{
    QTextDocument document;
    document.setPlainText(QStringLiteral("#include \"copilot_provider.h\"\n"
                                         "void free_function();\n"));

    QString error;
    QStringList trace;
    const QString block = build_completion_clang_context_block(
        nullptr, Utils::FilePath::fromString(QStringLiteral("/tmp/provider.cpp")), &document, 5,
        &error, &trace);

    QVERIFY(error.isEmpty());
    QVERIFY(block.contains(QStringLiteral("Local C++ semantic context")));
    QVERIFY(trace.isEmpty() == false);
    QVERIFY(trace.join(QLatin1Char('\n')).contains(QStringLiteral("clangd service unavailable")));
}

void tst_completion_clang_context_t::workspace_symbol_matches_owner_accepts_qualified_name()
{
    completion_owner_symbol_t owner;
    owner.class_name = QStringLiteral("copilot_provider_t");
    owner.scope = QStringLiteral("qcai2");
    owner.qualified_class_name = QStringLiteral("qcai2::copilot_provider_t");

    clangd_symbol_match_t match;
    match.name = QStringLiteral("qcai2::copilot_provider_t");
    match.kind_name = QStringLiteral("class");
    match.link.file_path = Utils::FilePath::fromString(QStringLiteral("/tmp/copilot_provider.h"));
    match.link.line = 17;
    match.link.column = 7;

    QVERIFY(workspace_symbol_matches_owner(match, owner));
    QCOMPARE(workspace_symbol_qualified_name(match), QStringLiteral("qcai2::copilot_provider_t"));
}

void tst_completion_clang_context_t::
    workspace_symbol_matches_owner_accepts_container_qualified_name()
{
    completion_owner_symbol_t owner;
    owner.class_name = QStringLiteral("copilot_provider_t");
    owner.scope = QStringLiteral("qcai2");
    owner.qualified_class_name = QStringLiteral("qcai2::copilot_provider_t");

    clangd_symbol_match_t match;
    match.name = QStringLiteral("copilot_provider_t");
    match.container_name = QStringLiteral("qcai2");
    match.kind_name = QStringLiteral("class");
    match.link.file_path = Utils::FilePath::fromString(QStringLiteral("/tmp/copilot_provider.h"));
    match.link.line = 17;
    match.link.column = 7;

    QVERIFY(workspace_symbol_matches_owner(match, owner));
    QCOMPARE(workspace_symbol_qualified_name(match), QStringLiteral("qcai2::copilot_provider_t"));
}

void tst_completion_clang_context_t::find_class_context_via_text_scan_recovers_class_range()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString file_path = dir.filePath(QStringLiteral("copilot_provider.h"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("namespace qcai2\n"
               "{\n"
               "class copilot_provider_t : public QObject, public iai_provider_t\n"
               "{\n"
               "public:\n"
               "    void complete();\n"
               "};\n"
               "}\n");
    file.close();

    QStringList trace;
    const completion_class_context_t context = find_class_context_via_text_scan(
        Utils::FilePath::fromString(file_path), QStringLiteral("copilot_provider_t"),
        QStringLiteral("qcai2::copilot_provider_t"), 3, &trace);

    QVERIFY(context.is_valid());
    QCOMPARE(context.class_name, QStringLiteral("copilot_provider_t"));
    QCOMPARE(context.enclosing_scope, QStringLiteral("qcai2::copilot_provider_t"));
    QCOMPARE(context.class_range.start_line, 3);
    QCOMPARE(context.class_range.end_line, 7);
    QVERIFY(trace.join(QLatin1Char('\n')).contains(QStringLiteral("text_scan resolved")));
}

void tst_completion_clang_context_t::parse_completion_owner_symbol_extracts_qualified_owner()
{
    const completion_owner_symbol_t owner = parse_completion_owner_symbol(
        QStringLiteral("### instance-method `qcai2::copilot_provider_t::complete`"));

    QVERIFY(owner.is_valid());
    QCOMPARE(owner.class_name, QStringLiteral("copilot_provider_t"));
    QCOMPARE(owner.scope, QStringLiteral("qcai2"));
    QCOMPARE(owner.qualified_class_name, QStringLiteral("qcai2::copilot_provider_t"));
}

void tst_completion_clang_context_t::parse_completion_owner_symbol_uses_fallback_scope()
{
    const completion_owner_symbol_t owner = parse_completion_owner_symbol(
        QStringLiteral("### instance-method `copilot_provider_t::complete`"),
        QStringLiteral("qcai2"));

    QVERIFY(owner.is_valid());
    QCOMPARE(owner.class_name, QStringLiteral("copilot_provider_t"));
    QCOMPARE(owner.scope, QStringLiteral("qcai2"));
    QCOMPARE(owner.qualified_class_name, QStringLiteral("qcai2::copilot_provider_t"));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_completion_clang_context_t)

#include "tst_completion_clang_context.moc"
