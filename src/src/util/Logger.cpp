/*! @file
    @brief Implements the plugin's bounded in-memory logger.
*/

#include "Logger.h"

#include <QDateTime>

namespace qcai2
{

/**
 * @brief Constructs the logger with logging disabled by default.
 */
logger_t::logger_t() = default;

logger_t &logger_t::instance()
{
    static logger_t s;
    return s;
}

void logger_t::set_enabled(bool enabled)
{
    this->enabled = enabled;
}

void logger_t::log(level_t level, const QString &category, const QString &message)
{
    // Errors are always logged regardless of enabled flag
    if (((!this->enabled && level != ERROR) == true))
    {
        return;
    }

    const QString entry = this->format(level, category, message);

    {
        QMutexLocker lock(&this->mutex);
        this->retained_entries.append(entry);
        if (((this->retained_entries.size() > k_max_entries) == true))
        {
            this->retained_entries.removeFirst();
        }
    }

    emit this->entry_added(entry);
}

void logger_t::debug(const QString &category, const QString &message)
{
    this->log(DEBUG, category, message);
}

void logger_t::info(const QString &category, const QString &message)
{
    this->log(INFO, category, message);
}

void logger_t::warn(const QString &category, const QString &message)
{
    this->log(WARNING, category, message);
}

void logger_t::error(const QString &category, const QString &message)
{
    this->log(ERROR, category, message);
}

QStringList logger_t::log_entries() const
{
    QMutexLocker lock(&this->mutex);
    return this->retained_entries;
}

void logger_t::clear()
{
    QMutexLocker lock(&this->mutex);
    this->retained_entries.clear();
}

QString logger_t::level_str(level_t level)
{
    switch (level)
    {
        case DEBUG:
            return QStringLiteral("DBG");
        case INFO:
            return QStringLiteral("INF");
        case WARNING:
            return QStringLiteral("WRN");
        case ERROR:
            return QStringLiteral("ERR");
    }
    return QStringLiteral("???");
}

/**
 * @brief Builds the final stored log line.
 * @param level Severity level for the message.
 * @param category Source category shown in the line.
 * @param message Message payload to append after the metadata.
 * @return Timestamped, formatted log entry.
 */
QString logger_t::format(level_t level, const QString &category, const QString &message) const
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
             this->level_str(level), category, message);
}

}  // namespace qcai2
