#pragma once

/*! @file
    @brief In-memory logging utilities used by the plugin and its UI.
*/

#include <QMutex>
#include <QObject>
#include <QString>

namespace qcai2
{

/**
 * @brief Collects formatted log messages and exposes them through signals.
 *
 * The logger stores a bounded history for UI display and can suppress
 * non-error messages when debug logging is disabled.
 */
class logger_t : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Severity levels supported by the logger.
     */
    enum level_t : std::uint8_t
    {
        /** @brief Verbose diagnostic output. */
        DEBUG,
        /** @brief Routine informational messages. */
        INFO,
        /** @brief Recoverable problems or suspicious conditions. */
        WARNING,
        /** @brief Failures that are always recorded. */
        ERROR
    };

    /**
     * @brief Returns the process-wide logger instance.
     * @return Singleton logger used across the plugin.
     */
    static logger_t &instance();

    /**
     * @brief Enables or disables non-error logging.
     * @param enabled True to keep debug, info, and warning entries.
     */
    void set_enabled(bool enabled);

    /**
     * @brief Reports whether non-error logging is enabled.
     * @return True when debug, info, and warning entries are accepted.
     */
    bool is_enabled() const
    {
        return this->enabled;
    }

    /**
     * @brief Adds a formatted log entry.
     * @param level Severity level for the message.
     * @param category Short source category shown in the entry text.
     * @param message Log payload.
     */
    void log(level_t level, const QString &category, const QString &message);

    /**
     * @brief Logs a debug entry when logging is enabled.
     * @param category Short source category shown in the entry text.
     * @param message Log payload.
     */
    void debug(const QString &category, const QString &message);

    /**
     * @brief Logs an informational entry when logging is enabled.
     * @param category Short source category shown in the entry text.
     * @param message Log payload.
     */
    void info(const QString &category, const QString &message);

    /**
     * @brief Logs a warning entry when logging is enabled.
     * @param category Short source category shown in the entry text.
     * @param message Log payload.
     */
    void warn(const QString &category, const QString &message);

    /**
     * @brief Logs an error entry regardless of the enabled flag.
     * @param category Short source category shown in the entry text.
     * @param message Log payload.
     */
    void error(const QString &category, const QString &message);

    /**
     * @brief Returns a snapshot of the retained log history.
     * @return Thread-safe copy of the formatted entries buffer.
     */
    QStringList log_entries() const;

    /**
     * @brief Removes all retained log entries.
     */
    void clear();

signals:
    /**
     * @brief Emitted after a formatted entry is appended.
     * @param formatted Newly appended log line.
     */
    void entry_added(const QString &formatted);

private:
    /**
     * @brief Creates the singleton logger instance.
     */
    logger_t();

    /**
     * @brief Converts a severity level to its short tag.
     * @param level Severity level to stringify.
     * @return Three-letter level tag used in formatted entries.
     */
    static QString level_str(level_t level);

    /**
     * @brief Formats a log entry for storage and display.
     * @param level Severity level for the message.
     * @param category Source category shown in the entry text.
     * @param message Log payload.
     * @return Timestamped log line.
     */
    QString format(level_t level, const QString &category, const QString &message) const;

    /** @brief Guards access to the retained entries buffer. */
    mutable QMutex mutex;

    /** @brief Ring buffer of formatted log lines. */
    QStringList retained_entries;

    /** @brief Enables non-error logging when true. */
    bool enabled = false;

    /** @brief Maximum retained log entries. */
    static constexpr int entries_max = 5000;
};

/** @brief Shortcut for the process-wide logger singleton. */
#define qcLog qcai2::logger_t::instance()

/** @brief Logs a debug message when logging is enabled. */
#define QCAI_DEBUG(cat, msg)                                                                      \
    do                                                                                            \
    {                                                                                             \
        if (qcLog.is_enabled())                                                                   \
            qcLog.debug(cat, msg);                                                                \
    } while (0)

/** @brief Logs an informational message when logging is enabled. */
#define QCAI_INFO(cat, msg)                                                                       \
    do                                                                                            \
    {                                                                                             \
        if (qcLog.is_enabled())                                                                   \
            qcLog.info(cat, msg);                                                                 \
    } while (0)

/** @brief Logs a warning message when logging is enabled. */
#define QCAI_WARN(cat, msg)                                                                       \
    do                                                                                            \
    {                                                                                             \
        if (qcLog.is_enabled())                                                                   \
            qcLog.warn(cat, msg);                                                                 \
    } while (0)

/** @brief Logs an error message regardless of logger enablement. */
#define QCAI_ERROR(cat, msg) qcLog.error(cat, msg)

}  // namespace qcai2
