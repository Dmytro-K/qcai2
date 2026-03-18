#include "OutputCaptureSupport.h"

#include <QRegularExpression>

namespace qcai2
{

namespace
{

QString severity_label(diagnostic_severity_t severity)
{
    switch (severity)
    {
        case diagnostic_severity_t::ERROR:
            return QStringLiteral("error");
        case diagnostic_severity_t::WARNING:
            return QStringLiteral("warning");
        case diagnostic_severity_t::NOTE:
            return QStringLiteral("note");
        case diagnostic_severity_t::UNKNOWN:
            break;
    }

    return QStringLiteral("unknown");
}

diagnostic_severity_t severity_from_text(const QString &severity)
{
    if (((severity.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0) == true))
    {
        return diagnostic_severity_t::ERROR;
    }
    if (((severity.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0) == true))
    {
        return diagnostic_severity_t::WARNING;
    }
    if (((severity.compare(QStringLiteral("note"), Qt::CaseInsensitive) == 0) == true))
    {
        return diagnostic_severity_t::NOTE;
    }
    return diagnostic_severity_t::UNKNOWN;
}

QString format_location(const captured_diagnostic_t &diagnostic)
{
    if (diagnostic.file_path.isEmpty())
    {
        return {};
    }

    QString location = diagnostic.file_path;
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

bounded_text_buffer_t::bounded_text_buffer_t(int max_chars) : max_chars(max_chars)
{
}

void bounded_text_buffer_t::append_chunk(const QString &text, bool append_newline)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (((append_newline && !normalized.endsWith(QLatin1Char('\n'))) == true))
    {
        normalized += QLatin1Char('\n');
    }

    const QString combined = this->pending + normalized;
    this->pending.clear();

    qsizetype lineStart = 0;
    while (lineStart < combined.size())
    {
        const qsizetype newlinePos = combined.indexOf(QLatin1Char('\n'), lineStart);
        if (newlinePos < 0)
        {
            this->pending = combined.mid(lineStart);
            break;
        }

        this->append_line(combined.mid(lineStart, newlinePos - lineStart));
        lineStart = newlinePos + 1;
    }

    this->trim_to_limit();
}

void bounded_text_buffer_t::clear()
{
    this->lines.clear();
    this->pending.clear();
    this->total_chars = 0;
}

bool bounded_text_buffer_t::is_empty() const
{
    return this->lines.isEmpty() && this->pending.isEmpty();
}

QString bounded_text_buffer_t::text() const
{
    QStringList all_lines = this->lines;
    if (((!this->pending.isEmpty()) == true))
    {
        all_lines.append(this->pending);
    }
    return all_lines.join(QLatin1Char('\n'));
}

QString bounded_text_buffer_t::last_lines(int maxLines) const
{
    QStringList all_lines = this->lines;
    if (((!this->pending.isEmpty()) == true))
    {
        all_lines.append(this->pending);
    }

    if (maxLines > 0 && all_lines.size() > maxLines)
    {
        all_lines = all_lines.mid(all_lines.size() - maxLines);
    }

    return all_lines.join(QLatin1Char('\n'));
}

void bounded_text_buffer_t::append_line(const QString &line)
{
    this->lines.append(line);
    this->total_chars += line.size() + 1;
    this->trim_to_limit();
}

void bounded_text_buffer_t::trim_to_limit()
{
    if (((this->max_chars <= 0) == true))
    {
        return;
    }

    while (((!this->lines.isEmpty() && this->total_chars > this->max_chars) == true))
    {
        this->total_chars -= this->lines.front().size() + 1;
        this->lines.removeFirst();
    }
}

QList<captured_diagnostic_t> extract_diagnostics_from_text(const QString &text)
{
    static const QRegularExpression diagnosticRe(
        R"(^\s*(?:(.+?):(\d+)(?::(\d+))?:\s*)?(error|warning|note):\s*(.+?)\s*$)",
        QRegularExpression::CaseInsensitiveOption);

    QList<captured_diagnostic_t> diagnostics;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        const QRegularExpressionMatch match = diagnosticRe.match(line);
        if (!match.hasMatch())
        {
            continue;
        }

        captured_diagnostic_t diagnostic;
        diagnostic.file_path = match.captured(1).trimmed();
        diagnostic.line = match.captured(2).toInt();
        diagnostic.column = match.captured(3).toInt();
        diagnostic.severity = severity_from_text(match.captured(4));
        diagnostic.summary = match.captured(5).trimmed();
        diagnostics.append(diagnostic);
    }

    return diagnostics;
}

QString format_captured_diagnostics(const QList<captured_diagnostic_t> &diagnostics, int maxItems)
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
        const captured_diagnostic_t &diagnostic = diagnostics.at(i);
        QString line = QStringLiteral("[%1] ").arg(severity_label(diagnostic.severity));

        const QString location = format_location(diagnostic);
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
