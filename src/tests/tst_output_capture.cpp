/*! @file
    @brief Unit tests for buffered IDE output capture helpers.
*/

#include <QtTest>

#include "src/util/OutputCaptureSupport.h"

using namespace qcai2;

class output_capture_test_t : public QObject
{
    Q_OBJECT

private slots:
    void bounded_buffer_preserves_chunks_and_newline_hints();
    void bounded_buffer_trims_oldest_lines_when_full();
    void extract_diagnostics_parses_compiler_style_locations();
    void format_diagnostics_includes_location_and_details();
};

void output_capture_test_t::bounded_buffer_preserves_chunks_and_newline_hints()
{
    bounded_text_buffer_t buffer(1024);

    buffer.append_chunk(QStringLiteral("first"), true);
    buffer.append_chunk(QStringLiteral("second\nthird"), false);

    QCOMPARE(buffer.last_lines(10), QStringLiteral("first\nsecond\nthird"));
}

void output_capture_test_t::bounded_buffer_trims_oldest_lines_when_full()
{
    bounded_text_buffer_t buffer(12);

    buffer.append_chunk(QStringLiteral("1111"), true);
    buffer.append_chunk(QStringLiteral("2222"), true);
    buffer.append_chunk(QStringLiteral("3333"), true);

    QCOMPARE(buffer.last_lines(10), QStringLiteral("2222\n3333"));
}

void output_capture_test_t::extract_diagnostics_parses_compiler_style_locations()
{
    const QList<captured_diagnostic_t> diagnostics = extract_diagnostics_from_text(
        QStringLiteral("src/main.cpp:17:4: error: missing semicolon\nwarning: unused variable"));

    QCOMPARE(diagnostics.size(), 2);
    QCOMPARE(diagnostics.at(0).file_path, QStringLiteral("src/main.cpp"));
    QCOMPARE(diagnostics.at(0).line, 17);
    QCOMPARE(diagnostics.at(0).column, 4);
    QCOMPARE(diagnostics.at(0).severity, diagnostic_severity_t::ERROR);
    QCOMPARE(diagnostics.at(1).severity, diagnostic_severity_t::WARNING);
}

void output_capture_test_t::format_diagnostics_includes_location_and_details()
{
    const QList<captured_diagnostic_t> diagnostics{
        captured_diagnostic_t{diagnostic_severity_t::WARNING, QStringLiteral("deprecated API"),
                              QStringLiteral("use the new overload"),
                              QStringLiteral("src/lib.cpp"), QStringLiteral("clang"), 9, 2}};

    const QString rendered = format_captured_diagnostics(diagnostics, 10);

    QVERIFY(rendered.contains(QStringLiteral("[warning] src/lib.cpp:9:2")));
    QVERIFY(rendered.contains(QStringLiteral("deprecated API")));
    QVERIFY(rendered.contains(QStringLiteral("origin: clang")));
    QVERIFY(rendered.contains(QStringLiteral("details: use the new overload")));
}

QTEST_APPLESS_MAIN(output_capture_test_t)

#include "tst_output_capture.moc"
