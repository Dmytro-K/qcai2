/*! Declares the transport abstraction used by the Qt MCP client. */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace qtmcp
{

class transport_t : public QObject
{
    Q_OBJECT

public:
    enum class state_t
    {
        DISCONNECTED,
        STARTING,
        CONNECTED,
        STOPPING,
    };
    Q_ENUM(state_t)

    explicit transport_t(QObject *parent = nullptr);
    ~transport_t() override;

    virtual QString transport_name() const = 0;
    virtual state_t state() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool send_message(const QJsonObject &message) = 0;

signals:
    void started();
    void stopped();
    void state_changed(qtmcp::transport_t::state_t state);
    void message_received(const QJsonObject &message);
    void error_occurred(const QString &message);
    void log_message(const QString &message);
};

}  // namespace qtmcp

Q_DECLARE_METATYPE(qtmcp::transport_t::state_t)
