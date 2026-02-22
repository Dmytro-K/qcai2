#pragma once

#include <QObject>
#include <QString>
#include <QMutex>

namespace Qcai2 {

// Centralized logger for the AI Agent plugin.
// Messages are stored in a ring buffer and emitted via signal for UI display.
// Logging is controlled by the debugLogging setting.
class Logger : public QObject
{
    Q_OBJECT
public:
    enum Level { Debug, Info, Warning, Error };

    static Logger &instance();

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void log(Level level, const QString &category, const QString &message);
    void debug(const QString &category, const QString &message);
    void info(const QString &category, const QString &message);
    void warn(const QString &category, const QString &message);
    void error(const QString &category, const QString &message);

    // Get all log entries (thread-safe copy)
    QStringList entries() const;
    void clear();

signals:
    void entryAdded(const QString &formatted);

private:
    Logger();

    static QString levelStr(Level level);
    QString format(Level level, const QString &category, const QString &message) const;

    mutable QMutex m_mutex;
    QStringList m_entries;
    bool m_enabled = false;
    static constexpr int kMaxEntries = 5000;
};

// Convenience macros
#define qcLog   Qcai2::Logger::instance()
#define QCAI_DEBUG(cat, msg) do { if (qcLog.isEnabled()) qcLog.debug(cat, msg); } while(0)
#define QCAI_INFO(cat, msg)  do { if (qcLog.isEnabled()) qcLog.info(cat, msg);  } while(0)
#define QCAI_WARN(cat, msg)  do { if (qcLog.isEnabled()) qcLog.warn(cat, msg);  } while(0)
#define QCAI_ERROR(cat, msg) qcLog.error(cat, msg)

} // namespace Qcai2
