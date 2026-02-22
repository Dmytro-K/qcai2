#include "Logger.h"

#include <QDateTime>

namespace Qcai2 {

Logger::Logger() = default;

Logger &Logger::instance()
{
    static Logger s;
    return s;
}

void Logger::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void Logger::log(Level level, const QString &category, const QString &message)
{
    // Errors are always logged regardless of enabled flag
    if (!m_enabled && level != Error)
        return;

    const QString entry = format(level, category, message);

    {
        QMutexLocker lock(&m_mutex);
        m_entries.append(entry);
        if (m_entries.size() > kMaxEntries)
            m_entries.removeFirst();
    }

    emit entryAdded(entry);
}

void Logger::debug(const QString &category, const QString &message)
{
    log(Debug, category, message);
}

void Logger::info(const QString &category, const QString &message)
{
    log(Info, category, message);
}

void Logger::warn(const QString &category, const QString &message)
{
    log(Warning, category, message);
}

void Logger::error(const QString &category, const QString &message)
{
    log(Error, category, message);
}

QStringList Logger::entries() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries;
}

void Logger::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
}

QString Logger::levelStr(Level level)
{
    switch (level) {
    case Debug:   return QStringLiteral("DBG");
    case Info:    return QStringLiteral("INF");
    case Warning: return QStringLiteral("WRN");
    case Error:   return QStringLiteral("ERR");
    }
    return QStringLiteral("???");
}

QString Logger::format(Level level, const QString &category, const QString &message) const
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
             levelStr(level),
             category,
             message);
}

} // namespace Qcai2
