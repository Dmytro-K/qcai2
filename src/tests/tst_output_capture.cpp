/*! @file
    @brief Unit tests for buffered IDE output capture helpers.
*/

#include <QtTest>

#include "src/util/OutputCaptureSupport.h"

using namespace qcai2;

class OutputCaptureTest : public QObject
{
    Q_OBJECT

private slots:
    void boundedBuffer_preservesChunksAndNewlineHints();
    void boundedBuffer_trimsOldestLinesWhenFull();
    void extractDiagnostics_parsesCompilerStyleLocations();
    void formatDiagnostics_includesLocationAndDetails();
};

void OutputCaptureTest::boundedBuffer_preservesChunksAndNewlineHints()
{
    BoundedTextBuffer buffer(1024);

    buffer.appendChunk(QStringLiteral("first"), true);
    buffer.appendChunk(QStringLiteral("second\nthird"), false);

    QCOMPARE(buffer.lastLines(10), QStringLiteral("first\nsecond\nthird"));
}

void OutputCaptureTest::boundedBuffer_trimsOldestLinesWhenFull()
{
    BoundedTextBuffer buffer(12);

    buffer.appendChunk(QStringLiteral("1111"), true);
    buffer.appendChunk(QStringLiteral("2222"), true);
    buffer.appendChunk(QStringLiteral("3333"), true);

    QCOMPARE(buffer.lastLines(10), QStringLiteral("2222\n3333"));
}

void OutputCaptureTest::extractDiagnostics_parsesCompilerStyleLocations()
{
    const QList<CapturedDiagnostic> diagnostics = extractDiagnosticsFromText(
        QStringLiteral("src/main.cpp:17:4: error: missing semicolon\nwarning: unused variable"));

    QCOMPARE(diagnostics.size(), 2);
    QCOMPARE(diagnostics.at(0).filePath, QStringLiteral("src/main.cpp"));
    QCOMPARE(diagnostics.at(0).line, 17);
    QCOMPARE(diagnostics.at(0).column, 4);
    QCOMPARE(diagnostics.at(0).severity, DiagnosticSeverity::Error);
    QCOMPARE(diagnostics.at(1).severity, DiagnosticSeverity::Warning);
}

void OutputCaptureTest::formatDiagnostics_includesLocationAndDetails()
{
    const QList<CapturedDiagnostic> diagnostics{CapturedDiagnostic{
        DiagnosticSeverity::Warning,
        QStringLiteral("deprecated API"),
        QStringLiteral("use the new overload"),
        QStringLiteral("src/lib.cpp"),
        QStringLiteral("clang"),
        9,
        2}};

    const QString rendered = formatCapturedDiagnostics(diagnostics, 10);

    QVERIFY(rendered.contains(QStringLiteral("[warning] src/lib.cpp:9:2")));
    QVERIFY(rendered.contains(QStringLiteral("deprecated API")));
    QVERIFY(rendered.contains(QStringLiteral("origin: clang")));
    QVERIFY(rendered.contains(QStringLiteral("details: use the new overload")));
}

QTEST_APPLESS_MAIN(OutputCaptureTest)

#include "tst_output_capture.moc"
