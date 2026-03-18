#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace qcai2
{

enum class diagnostic_severity_t : std::uint8_t
{
    ERROR,
    WARNING,
    NOTE,
    UNKNOWN
};

struct captured_diagnostic_t
{
    diagnostic_severity_t severity = diagnostic_severity_t::UNKNOWN;
    QString summary;
    QString details;
    QString file_path;
    QString origin;
    int line = -1;
    int column = 0;
};

class bounded_text_buffer_t
{
public:
    explicit bounded_text_buffer_t(int max_chars = 120000);

    void append_chunk(const QString &text, bool append_newline = false);
    void clear();

    bool is_empty() const;
    QString text() const;
    QString last_lines(int max_lines) const;

private:
    void append_line(const QString &line);
    void trim_to_limit();

    QStringList lines;
    QString pending;
    qsizetype max_chars = 0;
    qsizetype total_chars = 0;
};

QList<captured_diagnostic_t> extract_diagnostics_from_text(const QString &text);
QString format_captured_diagnostics(const QList<captured_diagnostic_t> &diagnostics,
                                    int max_items = 50);

}  // namespace qcai2
