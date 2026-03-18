/*! Declares the transport-agnostic JSON-RPC client core for MCP. */
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <memory>

namespace qtmcp
{

class transport_t;

class client_t : public QObject
{
    Q_OBJECT

public:
    explicit client_t(QObject *parent = nullptr);
    ~client_t() override;

    void set_transport(std::unique_ptr<transport_t> transport);
    transport_t *transport() const;
    bool is_connected() const;

    void start();
    void stop();

    qint64 send_request(const QString &method, const QJsonValue &params = QJsonValue());
    bool send_notification(const QString &method, const QJsonValue &params = QJsonValue());
    bool send_result(const QJsonValue &id, const QJsonValue &result);
    bool send_error(const QJsonValue &id, int code, const QString &message,
                    const QJsonValue &data = QJsonValue());

signals:
    void connected();
    void disconnected();
    void transport_error_occurred(const QString &message);
    void transport_log_message(const QString &message);
    void request_received(const QJsonValue &id, const QString &method, const QJsonValue &params);
    void notification_received(const QString &method, const QJsonValue &params);
    void response_received(const QJsonValue &id, const QJsonValue &result);
    void error_response_received(const QJsonValue &id, int code, const QString &message,
                                 const QJsonValue &data);

private:
    bool send_envelope(QJsonObject message);
    void attach_transport();
    void handle_transport_message(const QJsonObject &message);

    std::unique_ptr<transport_t> current_transport;
    qint64 next_request_id = 1;
};

}  // namespace qtmcp
