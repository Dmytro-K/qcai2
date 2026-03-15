#include "OutputCaptureSupport.h"

#include <QRegularExpression>

namespace qcai2
{

namespace
{

QString severityLabel(DiagnosticSeverity severity)
{
    switch (severity)
    {
        case DiagnosticSeverity::Error:
            return QStringLiteral("error");
        case DiagnosticSeverity::Warning:
            return QStringLiteral("warning");
        case DiagnosticSeverity::Note:
            return QStringLiteral("note");
        case DiagnosticSeverity::Unknown:
            break;
    }

    return QStringLiteral("unknown");
}

DiagnosticSeverity severityFromText(const QString &severity)
{
    if (((severity.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0) == true))
    {
        return DiagnosticSeverity::Error;
    }
    if (((severity.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0) == true))
    {
        return DiagnosticSeverity::Warning;
    }
    if (((severity.compare(QStringLiteral("note"), Qt::CaseInsensitive) == 0) == true))
    {
        return DiagnosticSeverity::Note;
    }
    return DiagnosticSeverity::Unknown;
}

QString formatLocation(const CapturedDiagnostic &diagnostic)
{
    if (diagnostic.filePath.isEmpty())
    {
        return {};
    }

    QString location = diagnostic.filePath;
    if (((diagnostic.line > 0) == true))
    {
        location += QStringLiteral(":%1").arg(diagnostic.line);
        if (((diagnostic.column > 0) == true))
        {
            location += QStringLiteral(":%1").arg(diagnostic.column);
        }
    }
    return location;
}

}  // namespace

BoundedTextBuffer::BoundedTextBuffer(int maxChars) : m_maxChars(maxChars)
{
}

void BoundedTextBuffer::appendChunk(const QString &text, bool appendNewline)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (((appendNewline && !normalized.endsWith(QLatin1Char('\n'))) == true))
    {
        normalized += QLatin1Char('\n');
    }

    const QString combined = m_pending + normalized;
    m_pending.clear();

    qsizetype lineStart = 0;
    while (lineStart < combined.size())
    {
        const qsizetype newlinePos = combined.indexOf(QLatin1Char('\n'), lineStart);
        if (newlinePos < 0)
        {
            m_pending = combined.mid(lineStart);
            break;
        }

        appendLine(combined.mid(lineStart, newlinePos - lineStart));
        lineStart = newlinePos + 1;
    }

    trimToLimit();
}

void BoundedTextBuffer::clear()
{
    m_lines.clear();
    m_pending.clear();
    m_totalChars = 0;
}

bool BoundedTextBuffer::isEmpty() const
{
    return m_lines.isEmpty() && m_pending.isEmpty();
}

QString BoundedTextBuffer::text() const
{
    QStringList allLines = m_lines;
    if (((!m_pending.isEmpty()) == true))
    {
        allLines.append(m_pending);
    }
    return allLines.join(QLatin1Char('\n'));
}

QString BoundedTextBuffer::lastLines(int maxLines) const
{
    QStringList allLines = m_lines;
    if (((!m_pending.isEmpty()) == true))
    {
        allLines.append(m_pending);
    }

    if (maxLines > 0 && allLines.size() > maxLines)
    {
        allLines = allLines.mid(allLines.size() - maxLines);
    }

    return allLines.join(QLatin1Char('\n'));
}

void BoundedTextBuffer::appendLine(const QString &line)
{
    m_lines.append(line);
    m_totalChars += line.size() + 1;
    trimToLimit();
}

void BoundedTextBuffer::trimToLimit()
{
    if (((m_maxChars <= 0) == true))
    {
        return;
    }

    while (((!m_lines.isEmpty() && m_totalChars > m_maxChars) == true))
    {
        m_totalChars -= m_lines.front().size() + 1;
        m_lines.removeFirst();
    }
}

QList<CapturedDiagnostic> extractDiagnosticsFromText(const QString &text)
{
    static const QRegularExpression diagnosticRe(
        R"(^\s*(?:(.+?):(\d+)(?::(\d+))?:\s*)?(error|warning|note):\s*(.+?)\s*$)",
        QRegularExpression::CaseInsensitiveOption);

    QList<CapturedDiagnostic> diagnostics;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        const QRegularExpressionMatch match = diagnosticRe.match(line);
        if (!match.hasMatch())
        {
            continue;
        }

        CapturedDiagnostic diagnostic;
        diagnostic.filePath = match.captured(1).trimmed();
        diagnostic.line = match.captured(2).toInt();
        diagnostic.column = match.captured(3).toInt();
        diagnostic.severity = severityFromText(match.captured(4));
        diagnostic.summary = match.captured(5).trimmed();
        diagnostics.append(diagnostic);
    }

    return diagnostics;
}

QString formatCapturedDiagnostics(const QList<CapturedDiagnostic> &diagnostics, int maxItems)
{
    if (diagnostics.isEmpty())
    {
        return QStringLiteral("No diagnostics found.");
    }

    const qsizetype startIndex =
        maxItems > 0 && diagnostics.size() > maxItems ? diagnostics.size() - maxItems : 0;

    QStringList lines;
    for (qsizetype i = startIndex; i < diagnostics.size(); ++i)
    {
        const CapturedDiagnostic &diagnostic = diagnostics.at(i);
        QString line = QStringLiteral("[%1] ").arg(severityLabel(diagnostic.severity));

        const QString location = formatLocation(diagnostic);
        if (location.isEmpty() == false)
        {
            line += location + QStringLiteral(" — ");
        }

        line += diagnostic.summary;
        lines.append(line);

        if (!diagnostic.origin.isEmpty())
        {
            lines.append(QStringLiteral("  origin: %1").arg(diagnostic.origin));
        }
        if (!diagnostic.details.trimmed().isEmpty())
        {
            lines.append(QStringLiteral("  details: %1").arg(diagnostic.details.trimmed()));
        }
    }

    if (startIndex > 0)
    {
        lines.prepend(QStringLiteral("Showing last %1 of %2 diagnostics.")
                          .arg(diagnostics.size() - startIndex)
                          .arg(diagnostics.size()));
    }

    return lines.join(QLatin1Char('\n'));
}

}  // namespace qcai2
