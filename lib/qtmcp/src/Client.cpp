/*! Implements the transport-agnostic JSON-RPC client core for MCP. */

#include <qtmcp/Client.h>

#include <qtmcp/Transport.h>

#include <QJsonDocument>

#include <utility>

namespace qtmcp
{

namespace
{

constexpr int kMaxLoggedJsonChars = 2000;

QString formatJsonForLog(const QJsonObject &message)
{
    QString json = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    if (json.size() > kMaxLoggedJsonChars)
    {
        json = json.left(kMaxLoggedJsonChars);
        json += QStringLiteral("... [truncated]");
    }
    return json;
}

}  // namespace

Client::Client(QObject *parent) : QObject(parent)
{
}

Client::~Client() = default;

void Client::setTransport(std::unique_ptr<Transport> transport)
{
    if (m_transport != nullptr)
    {
        emit transportLogMessage(
            QStringLiteral("Replacing MCP transport `%1`.").arg(m_transport->transportName()));
        if (m_transport->state() != Transport::State::Disconnected)
            m_transport->stop();
        m_transport->disconnect(this);
    }

    m_transport = std::move(transport);
    if (m_transport != nullptr)
    {
        emit transportLogMessage(
            QStringLiteral("Configured MCP transport `%1`.").arg(m_transport->transportName()));
    }
    attachTransport();
}

Transport *Client::transport() const
{
    return m_transport.get();
}

bool Client::isConnected() const
{
    return m_transport != nullptr && m_transport->state() == Transport::State::Connected;
}

void Client::start()
{
    if (m_transport == nullptr)
    {
        emit transportErrorOccurred(
            QStringLiteral("Cannot start MCP client without a transport."));
        return;
    }

    emit transportLogMessage(QStringLiteral("Starting MCP client with transport `%1`.")
                                 .arg(m_transport->transportName()));
    m_transport->start();
}

void Client::stop()
{
    if (m_transport != nullptr)
    {
        emit transportLogMessage(QStringLiteral("Stopping MCP client transport `%1`.")
                                     .arg(m_transport->transportName()));
        m_transport->stop();
    }
}

qint64 Client::sendRequest(const QString &method, const QJsonValue &params)
{
    if (method.trimmed().isEmpty())
    {
        emit transportErrorOccurred(
            QStringLiteral("Cannot send an MCP request with an empty method."));
        return 0;
    }

    const qint64 requestId = m_nextRequestId++;
    QJsonObject message{
        {QStringLiteral("id"), requestId},
        {QStringLiteral("method"), method},
    };

    if (!(params.isUndefined() || params.isNull()))
    {
        message.insert(QStringLiteral("params"), params);
    }

    if (!sendEnvelope(message))
    {
        return 0;
    }
    emit transportLogMessage(
        QStringLiteral("MCP request sent: id=%1 method=%2").arg(requestId).arg(method));
    return requestId;
}

bool Client::sendNotification(const QString &method, const QJsonValue &params)
{
    if (method.trimmed().isEmpty())
    {
        emit transportErrorOccurred(
            QStringLiteral("Cannot send an MCP notification with an empty method."));
        return false;
    }

    QJsonObject message{
        {QStringLiteral("method"), method},
    };

    if (!(params.isUndefined() || params.isNull()))
    {
        message.insert(QStringLiteral("params"), params);
    }

    const bool ok = sendEnvelope(message);

    if (ok)
    {
        emit transportLogMessage(QStringLiteral("MCP notification sent: method=%1").arg(method));
    }
    return ok;
}

bool Client::sendResult(const QJsonValue &id, const QJsonValue &result)
{
    if (id.isUndefined())
    {
        emit transportErrorOccurred(QStringLiteral("Cannot send an MCP result without an id."));
        return false;
    }

    QJsonObject message{
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    };
    const bool ok = sendEnvelope(message);
    if (ok)
        emit transportLogMessage(QStringLiteral("MCP result sent."));
    return ok;
}

bool Client::sendError(const QJsonValue &id, int code, const QString &message,
                       const QJsonValue &data)
{
    if (id.isUndefined())
    {
        emit transportErrorOccurred(QStringLiteral("Cannot send an MCP error without an id."));
        return false;
    }

    QJsonObject errorObject{
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
    };
    if (!data.isUndefined())
        errorObject.insert(QStringLiteral("data"), data);

    QJsonObject envelope{
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), errorObject},
    };
    const bool ok = sendEnvelope(envelope);
    if (ok)
    {
        emit transportLogMessage(
            QStringLiteral("MCP error response sent: code=%1 message=%2").arg(code).arg(message));
    }
    return ok;
}

bool Client::sendEnvelope(QJsonObject message)
{
    if (m_transport == nullptr)
    {
        emit transportErrorOccurred(
            QStringLiteral("Cannot send an MCP message without a transport."));
        return false;
    }

    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    emit transportLogMessage(QStringLiteral("MCP -> %1").arg(formatJsonForLog(message)));
    return m_transport->sendMessage(message);
}

void Client::attachTransport()
{
    if (m_transport == nullptr)
        return;

    m_transport->setParent(this);
    connect(m_transport.get(), &Transport::started, this, [this]() {
        emit transportLogMessage(QStringLiteral("MCP transport started."));
        emit connected();
    });
    connect(m_transport.get(), &Transport::stopped, this, [this]() {
        emit transportLogMessage(QStringLiteral("MCP transport stopped."));
        emit disconnected();
    });
    connect(m_transport.get(), &Transport::stateChanged, this, [this](Transport::State state) {
        QString stateLabel = QStringLiteral("unknown");
        switch (state)
        {
            case Transport::State::Disconnected:
                stateLabel = QStringLiteral("disconnected");
                break;
            case Transport::State::Starting:
                stateLabel = QStringLiteral("starting");
                break;
            case Transport::State::Connected:
                stateLabel = QStringLiteral("connected");
                break;
            case Transport::State::Stopping:
                stateLabel = QStringLiteral("stopping");
                break;
        }
        emit transportLogMessage(
            QStringLiteral("MCP transport state changed: %1").arg(stateLabel));
    });
    connect(m_transport.get(), &Transport::logMessage, this, &Client::transportLogMessage);
    connect(m_transport.get(), &Transport::errorOccurred, this, &Client::transportErrorOccurred);
    connect(m_transport.get(), &Transport::messageReceived, this, &Client::handleTransportMessage);
}

void Client::handleTransportMessage(const QJsonObject &message)
{
    emit transportLogMessage(QStringLiteral("MCP <- %1").arg(formatJsonForLog(message)));
    if (message.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0"))
    {
        emit transportErrorOccurred(QStringLiteral("Received a non-JSON-RPC-2.0 MCP message."));
        return;
    }

    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonValue method = message.value(QStringLiteral("method"));

    if (method.isString())
    {
        const QJsonValue params = message.value(QStringLiteral("params"));
        if (id.isUndefined())
        {
            emit transportLogMessage(
                QStringLiteral("MCP notification received: method=%1").arg(method.toString()));
            emit notificationReceived(method.toString(), params);
        }
        else
        {
            emit transportLogMessage(
                QStringLiteral("MCP request received: method=%1").arg(method.toString()));
            emit requestReceived(id, method.toString(), params);
        }
        return;
    }

    if (!id.isUndefined())
    {
        if (message.contains(QStringLiteral("error")))
        {
            const QJsonObject errorObject = message.value(QStringLiteral("error")).toObject();
            emit transportLogMessage(
                QStringLiteral("MCP error response received: code=%1 message=%2")
                    .arg(errorObject.value(QStringLiteral("code")).toInt())
                    .arg(errorObject.value(QStringLiteral("message")).toString()));
            emit errorResponseReceived(id, errorObject.value(QStringLiteral("code")).toInt(),
                                       errorObject.value(QStringLiteral("message")).toString(),
                                       errorObject.value(QStringLiteral("data")));
            return;
        }

        if (message.contains(QStringLiteral("result")))
        {
            emit transportLogMessage(QStringLiteral("MCP response received."));
            emit responseReceived(id, message.value(QStringLiteral("result")));
            return;
        }
    }

    emit transportErrorOccurred(QStringLiteral("Received an unrecognized MCP JSON-RPC message."));
}

}  // namespace qtmcp
