/*! Declares the transport-agnostic JSON-RPC client core for MCP. */
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <memory>

namespace qtmcp
{

class Transport;

class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    void setTransport(std::unique_ptr<Transport> transport);
    Transport *transport() const;
    bool isConnected() const;

    void start();
    void stop();

    qint64 sendRequest(const QString &method, const QJsonValue &params = QJsonValue());
    bool sendNotification(const QString &method, const QJsonValue &params = QJsonValue());
    bool sendResult(const QJsonValue &id, const QJsonValue &result);
    bool sendError(const QJsonValue &id, int code, const QString &message,
                   const QJsonValue &data = QJsonValue());

signals:
    void connected();
    void disconnected();
    void transportErrorOccurred(const QString &message);
    void transportLogMessage(const QString &message);
    void requestReceived(const QJsonValue &id, const QString &method, const QJsonValue &params);
    void notificationReceived(const QString &method, const QJsonValue &params);
    void responseReceived(const QJsonValue &id, const QJsonValue &result);
    void errorResponseReceived(const QJsonValue &id, int code, const QString &message,
                               const QJsonValue &data);

private:
    bool sendEnvelope(QJsonObject message);
    void attachTransport();
    void handleTransportMessage(const QJsonObject &message);

    std::unique_ptr<Transport> m_transport;
    qint64 m_nextRequestId = 1;
};

}  // namespace qtmcp
