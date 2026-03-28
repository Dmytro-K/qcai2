/*! Implements the transport-agnostic JSON-RPC client core for MCP. */

#include <qtmcp/client.h>

#include <qtmcp/transport.h>

#include <QJsonDocument>

#include <utility>

namespace qtmcp
{

namespace
{

constexpr int logged_json_chars_max = 2000;

QString format_json_for_log(const QJsonObject &message)
{
    QString json = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    if (json.size() > logged_json_chars_max)
    {
        json = json.left(logged_json_chars_max);
        json += QStringLiteral("... [truncated]");
    }
    return json;
}

}  // namespace

client_t::client_t(QObject *parent) : QObject(parent)
{
}

client_t::~client_t() = default;

void client_t::set_transport(std::unique_ptr<transport_t> transport)
{
    if (((this->current_transport != nullptr) == true))
    {
        emit this->transport_log_message(QStringLiteral("Replacing MCP transport `%1`.")
                                             .arg(this->current_transport->transport_name()));
        if (((this->current_transport->state() != transport_t::state_t::DISCONNECTED) == true))
        {
            this->current_transport->stop();
        }
        this->current_transport->disconnect(this);
    }

    this->current_transport = std::move(transport);
    if (((this->current_transport != nullptr) == true))
    {
        emit this->transport_log_message(QStringLiteral("Configured MCP transport `%1`.")
                                             .arg(this->current_transport->transport_name()));
    }
    this->attach_transport();
}

transport_t *client_t::transport() const
{
    return this->current_transport.get();
}

bool client_t::is_connected() const
{
    return this->current_transport != nullptr &&
           this->current_transport->state() == transport_t::state_t::CONNECTED;
}

void client_t::start()
{
    if (((this->current_transport == nullptr) == true))
    {
        emit this->transport_error_occurred(
            QStringLiteral("Cannot start MCP client without a transport."));
        return;
    }

    emit this->transport_log_message(QStringLiteral("Starting MCP client with transport `%1`.")
                                         .arg(this->current_transport->transport_name()));
    this->current_transport->start();
}

void client_t::stop()
{
    if (((this->current_transport != nullptr) == true))
    {
        emit this->transport_log_message(QStringLiteral("Stopping MCP client transport `%1`.")
                                             .arg(this->current_transport->transport_name()));
        this->current_transport->stop();
    }
}

qint64 client_t::send_request(const QString &method, const QJsonValue &params)
{
    if (((method.trimmed().isEmpty()) == true))
    {
        emit this->transport_error_occurred(
            QStringLiteral("Cannot send an MCP request with an empty method."));
        return 0;
    }

    const qint64 request_id = this->next_request_id++;
    QJsonObject message{
        {QStringLiteral("id"), request_id},
        {QStringLiteral("method"), method},
    };

    if ((((params.isUndefined() || params.isNull())) == false))
    {
        message.insert(QStringLiteral("params"), params);
    }

    if (this->send_envelope(message) == false)
    {
        return 0;
    }
    emit this->transport_log_message(
        QStringLiteral("MCP request sent: id=%1 method=%2").arg(request_id).arg(method));
    return request_id;
}

bool client_t::send_notification(const QString &method, const QJsonValue &params)
{
    if (((method.trimmed().isEmpty()) == true))
    {
        emit this->transport_error_occurred(
            QStringLiteral("Cannot send an MCP notification with an empty method."));
        return false;
    }

    QJsonObject message{
        {QStringLiteral("method"), method},
    };

    if ((((params.isUndefined() || params.isNull())) == false))
    {
        message.insert(QStringLiteral("params"), params);
    }

    const bool ok = this->send_envelope(message);

    if (ok == true)
    {
        emit this->transport_log_message(
            QStringLiteral("MCP notification sent: method=%1").arg(method));
    }
    return ok;
}

bool client_t::send_result(const QJsonValue &id, const QJsonValue &result)
{
    if (id.isUndefined() == true)
    {
        emit this->transport_error_occurred(
            QStringLiteral("Cannot send an MCP result without an id."));
        return false;
    }

    QJsonObject message{
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    };
    const bool ok = this->send_envelope(message);
    if (ok == true)
    {
        emit this->transport_log_message(QStringLiteral("MCP result sent."));
    }
    return ok;
}

bool client_t::send_error(const QJsonValue &id, int code, const QString &message,
                          const QJsonValue &data)
{
    if (id.isUndefined() == true)
    {
        emit this->transport_error_occurred(
            QStringLiteral("Cannot send an MCP error without an id."));
        return false;
    }

    QJsonObject error_object{
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
    };
    if (data.isUndefined() == false)
    {
        error_object.insert(QStringLiteral("data"), data);
    }

    QJsonObject envelope{
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), error_object},
    };
    const bool ok = this->send_envelope(envelope);
    if (ok == true)
    {
        emit this->transport_log_message(
            QStringLiteral("MCP error response sent: code=%1 message=%2").arg(code).arg(message));
    }
    return ok;
}

bool client_t::send_envelope(QJsonObject message)
{
    if (((this->current_transport == nullptr) == true))
    {
        emit this->transport_error_occurred(
            QStringLiteral("Cannot send an MCP message without a transport."));
        return false;
    }

    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    emit this->transport_log_message(
        QStringLiteral("MCP -> %1").arg(format_json_for_log(message)));
    return this->current_transport->send_message(message);
}

void client_t::attach_transport()
{
    if (((this->current_transport == nullptr) == true))
    {
        return;
    }

    this->current_transport->setParent(this);
    connect(this->current_transport.get(), &transport_t::started, this, [this]() {
        emit this->transport_log_message(QStringLiteral("MCP transport started."));
        emit this->connected();
    });
    connect(this->current_transport.get(), &transport_t::stopped, this, [this]() {
        emit this->transport_log_message(QStringLiteral("MCP transport stopped."));
        emit this->disconnected();
    });
    connect(this->current_transport.get(), &transport_t::state_changed, this,
            [this](transport_t::state_t state) {
                QString state_label = QStringLiteral("unknown");
                switch (state)
                {
                    case transport_t::state_t::DISCONNECTED:
                        state_label = QStringLiteral("disconnected");
                        break;
                    case transport_t::state_t::STARTING:
                        state_label = QStringLiteral("starting");
                        break;
                    case transport_t::state_t::CONNECTED:
                        state_label = QStringLiteral("connected");
                        break;
                    case transport_t::state_t::STOPPING:
                        state_label = QStringLiteral("stopping");
                        break;
                }
                emit this->transport_log_message(
                    QStringLiteral("MCP transport state changed: %1").arg(state_label));
            });
    connect(this->current_transport.get(), &transport_t::log_message, this,
            &client_t::transport_log_message);
    connect(this->current_transport.get(), &transport_t::error_occurred, this,
            &client_t::transport_error_occurred);
    connect(this->current_transport.get(), &transport_t::message_received, this,
            &client_t::handle_transport_message);
}

void client_t::handle_transport_message(const QJsonObject &message)
{
    emit this->transport_log_message(
        QStringLiteral("MCP <- %1").arg(format_json_for_log(message)));
    if (((message.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) == true))
    {
        emit this->transport_error_occurred(
            QStringLiteral("Received a non-JSON-RPC-2.0 MCP message."));
        return;
    }

    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonValue method = message.value(QStringLiteral("method"));

    if (method.isString() == true)
    {
        const QJsonValue params = message.value(QStringLiteral("params"));
        if (id.isUndefined() == true)
        {
            emit this->transport_log_message(
                QStringLiteral("MCP notification received: method=%1").arg(method.toString()));
            emit this->notification_received(method.toString(), params);
        }
        else
        {
            emit this->transport_log_message(
                QStringLiteral("MCP request received: method=%1").arg(method.toString()));
            emit this->request_received(id, method.toString(), params);
        }
        return;
    }

    if (id.isUndefined() == false)
    {
        if (((message.contains(QStringLiteral("error"))) == true))
        {
            const QJsonObject error_object = message.value(QStringLiteral("error")).toObject();
            emit this->transport_log_message(
                QStringLiteral("MCP error response received: code=%1 message=%2")
                    .arg(error_object.value(QStringLiteral("code")).toInt())
                    .arg(error_object.value(QStringLiteral("message")).toString()));
            emit this->error_response_received(
                id, error_object.value(QStringLiteral("code")).toInt(),
                error_object.value(QStringLiteral("message")).toString(),
                error_object.value(QStringLiteral("data")));
            return;
        }

        if (((message.contains(QStringLiteral("result"))) == true))
        {
            emit this->transport_log_message(QStringLiteral("MCP response received."));
            emit this->response_received(id, message.value(QStringLiteral("result")));
            return;
        }
    }

    emit this->transport_error_occurred(
        QStringLiteral("Received an unrecognized MCP JSON-RPC message."));
}

}  // namespace qtmcp
