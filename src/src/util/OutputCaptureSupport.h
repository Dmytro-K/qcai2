#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace qcai2
{

enum class DiagnosticSeverity
{
    Error,
    Warning,
    Note,
    Unknown
};

struct CapturedDiagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Unknown;
    QString summary;
    QString details;
    QString filePath;
    QString origin;
    int line = -1;
    int column = 0;
};

class BoundedTextBuffer
{
public:
    explicit BoundedTextBuffer(int maxChars = 120000);

    void appendChunk(const QString &text, bool appendNewline = false);
    void clear();

    bool isEmpty() const;
    QString text() const;
    QString lastLines(int maxLines) const;

private:
    void appendLine(const QString &line);
    void trimToLimit();

    QStringList m_lines;
    QString m_pending;
    qsizetype m_maxChars = 0;
    qsizetype m_totalChars = 0;
};

QList<CapturedDiagnostic> extractDiagnosticsFromText(const QString &text);
QString formatCapturedDiagnostics(const QList<CapturedDiagnostic> &diagnostics, int maxItems = 50);

}  // namespace qcai2
