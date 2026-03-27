/*! @file
    @brief Regression tests for shared clang-derived completion context helpers.
*/

#include "../src/clangd/clangd_service.h"
#include "../src/completion/completion_clang_context.h"

#include <QtTest>

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

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_completion_clang_context_t)

#include "tst_completion_clang_context.moc"
