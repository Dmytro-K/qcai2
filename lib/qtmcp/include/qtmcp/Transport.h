/*! Declares the transport abstraction used by the Qt MCP client. */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace qtmcp
{

class Transport : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Disconnected,
        Starting,
        Connected,
        Stopping,
    };
    Q_ENUM(State)

    explicit Transport(QObject *parent = nullptr);
    ~Transport() override;

    virtual QString transportName() const = 0;
    virtual State state() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool sendMessage(const QJsonObject &message) = 0;

signals:
    void started();
    void stopped();
    void stateChanged(qtmcp::Transport::State state);
    void messageReceived(const QJsonObject &message);
    void errorOccurred(const QString &message);
    void logMessage(const QString &message);
};

}  // namespace qtmcp

Q_DECLARE_METATYPE(qtmcp::Transport::State)
