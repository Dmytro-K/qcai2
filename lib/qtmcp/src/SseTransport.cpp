/*! Implements the legacy SSE transport for the Qt MCP client.
 *
 * The legacy MCP SSE transport (protocol version 2024-11-05) works as follows:
 *  1. Client opens a persistent GET connection to the SSE endpoint.
 *  2. Server sends an `event: endpoint` with the POST URL for client messages.
 *  3. Client sends JSON-RPC messages via HTTP POST to that URL.
 *  4. Server delivers responses and notifications through the SSE stream.
 */
#include <qtmcp/SseTransport.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace qtmcp
{

SseTransport::SseTransport(SseTransportConfig config, QObject *parent)
    : Transport(parent), m_config(std::move(config)),
      m_networkAccessManager(new QNetworkAccessManager(this))
{
}

SseTransport::~SseTransport()
{
    stop();
}

QString SseTransport::transportName() const
{
    return QStringLiteral("sse");
}

Transport::State SseTransport::state() const
{
    return m_state;
}

const SseTransportConfig &SseTransport::config() const
{
    return m_config;
}

void SseTransport::start()
{
    if (m_state != State::Disconnected)
    {
        return;
    }

    const QString scheme = m_config.endpoint.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
    {
        emit errorOccurred(
            QStringLiteral("SSE transport requires http/https URL, got: %1").arg(scheme));
        return;
    }

    setState(State::Starting);
    m_postEndpoint.clear();
    openSseStream();
}

void SseTransport::stop()
{
    if (m_state == State::Disconnected)
    {
        return;
    }

    setState(State::Stopping);

    if (m_sseReply != nullptr)
    {
        m_sseReply->abort();
        m_sseReply->deleteLater();
        m_sseReply = nullptr;
    }

    m_sseBuffer.clear();
    m_currentEventData.clear();
    m_currentEventType.clear();
    m_postEndpoint.clear();

    setState(State::Disconnected);
    emit stopped();
}

bool SseTransport::sendMessage(const QJsonObject &message)
{
    if (m_state != State::Connected)
    {
        emit errorOccurred(QStringLiteral("Cannot send message: SSE transport not connected."));
        return false;
    }

    if (m_postEndpoint.isEmpty() == true)
    {
        emit errorOccurred(
            QStringLiteral("Cannot send message: server has not provided a POST endpoint yet."));
        return false;
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    emit logMessage(QStringLiteral("sse -> %1").arg(QString::fromUtf8(payload)));
    postMessage(payload);
    return true;
}

void SseTransport::setState(State state)
{
    if (m_state == state)
    {
        return;
    }

    m_state = state;
    QString stateLabel = QStringLiteral("unknown");
    switch (m_state)
    {
        case State::Disconnected:
            stateLabel = QStringLiteral("disconnected");
            break;
        case State::Starting:
            stateLabel = QStringLiteral("starting");
            break;
        case State::Connected:
            stateLabel = QStringLiteral("connected");
            break;
        case State::Stopping:
            stateLabel = QStringLiteral("stopping");
            break;
    }
    emit logMessage(QStringLiteral("sse state changed: %1").arg(stateLabel));
    emit stateChanged(m_state);
}

void SseTransport::openSseStream()
{
    QNetworkRequest request(m_config.endpoint);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    // No transfer timeout — the stream stays open indefinitely
    request.setTransferTimeout(0);
    applyConfiguredHeaders(request);

    emit logMessage(QStringLiteral("sse opening stream: %1").arg(m_config.endpoint.toString()));
    m_sseReply = m_networkAccessManager->get(request);

    connect(m_sseReply, &QNetworkReply::readyRead, this, &SseTransport::processStreamReadyRead);
    connect(m_sseReply, &QNetworkReply::finished, this, &SseTransport::processStreamFinished);
    connect(m_sseReply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError code) {
                emit logMessage(QStringLiteral("SSE stream error: code=%1 text=%2")
                                    .arg(static_cast<int>(code))
                                    .arg(m_sseReply != nullptr ? m_sseReply->errorString()
                                                               : QStringLiteral("(null reply)")));
            });
}

void SseTransport::processStreamReadyRead()
{
    if (m_sseReply == nullptr)
    {
        return;
    }

    const QByteArray chunk = m_sseReply->readAll();
    if (chunk.isEmpty() == true)
    {
        return;
    }

    m_sseBuffer += chunk;
    processSseBuffer(false);
}

void SseTransport::processStreamFinished()
{
    if (m_sseReply == nullptr)
    {
        return;
    }

    // Flush remaining buffer
    if (m_sseBuffer.isEmpty() == false)
    {
        processSseBuffer(true);
    }

    const int statusCode = m_sseReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errorString = m_sseReply->errorString();

    m_sseReply->deleteLater();
    m_sseReply = nullptr;

    if (m_state == State::Stopping || m_state == State::Disconnected)
    {
        return;
    }

    emit logMessage(
        QStringLiteral("SSE stream closed: status=%1 error=%2").arg(statusCode).arg(errorString));

    // Reconnect after delay
    if (m_config.reconnectDelayMs > 0)
    {
        emit logMessage(
            QStringLiteral("SSE reconnecting in %1 ms...").arg(m_config.reconnectDelayMs));
        QTimer::singleShot(m_config.reconnectDelayMs, this, [this]() {
            if (m_state != State::Stopping && m_state != State::Disconnected)
            {
                m_sseBuffer.clear();
                m_currentEventData.clear();
                m_currentEventType.clear();
                openSseStream();
            }
        });
    }
    else
    {
        emit errorOccurred(QStringLiteral("SSE stream closed unexpectedly."));
        setState(State::Disconnected);
        emit stopped();
    }
}

void SseTransport::processSseBuffer(bool flush)
{
    while (true)
    {
        const qsizetype newlineIndex = m_sseBuffer.indexOf('\n');
        if (newlineIndex < 0)
        {
            break;
        }

        QByteArray line = m_sseBuffer.left(newlineIndex);
        m_sseBuffer.remove(0, newlineIndex + 1);
        if (line.endsWith('\r') == true)
        {
            line.chop(1);
        }

        // Empty line = end of event
        if (line.isEmpty() == true)
        {
            handleSseEvent();
            continue;
        }

        // Comment line
        if (line.startsWith(':') == true)
        {
            continue;
        }

        if (line.startsWith("event:") == true)
        {
            m_currentEventType = QString::fromUtf8(line.mid(6)).trimmed();
            continue;
        }

        if (line.startsWith("data:") == true)
        {
            QByteArray data = line.mid(5);
            if (data.startsWith(' ') == true)
            {
                data.remove(0, 1);
            }
            if (m_currentEventData.isEmpty() == false)
            {
                m_currentEventData.append('\n');
            }
            m_currentEventData.append(data);
        }
    }

    if (flush && (!m_currentEventData.isEmpty() || !m_currentEventType.isEmpty()))
    {
        handleSseEvent();
    }
}

void SseTransport::handleSseEvent()
{
    const QByteArray data = m_currentEventData;
    const QString eventType = m_currentEventType;
    m_currentEventData.clear();
    m_currentEventType.clear();

    if (data.trimmed().isEmpty() == true)
    {
        return;
    }

    emit logMessage(QStringLiteral("sse event `%1` <- %2")
                        .arg(eventType.isEmpty() ? QStringLiteral("message") : eventType,
                             QString::fromUtf8(data).left(200)));

    // The server sends "endpoint" event with the POST URL
    if (eventType == QStringLiteral("endpoint"))
    {
        const QString endpointPath = QString::fromUtf8(data).trimmed();
        // Resolve relative URL against the SSE endpoint
        m_postEndpoint = m_config.endpoint.resolved(QUrl(endpointPath));
        emit logMessage(
            QStringLiteral("SSE received POST endpoint: %1").arg(m_postEndpoint.toString()));

        if (m_state == State::Starting)
        {
            setState(State::Connected);
            emit started();
        }
        return;
    }

    // Parse JSON-RPC message from the stream
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        emit errorOccurred(QStringLiteral("Failed to parse MCP SSE event JSON: %1")
                               .arg(parseError.errorString()));
        return;
    }

    if (document.isObject() == true)
    {
        emit messageReceived(document.object());
    }
    else if (document.isArray() == true)
    {
        const auto array = document.array();
        for (const auto &value : array)
        {
            if (value.isObject() == true)
            {
                emit messageReceived(value.toObject());
            }
        }
    }
}

void SseTransport::postMessage(const QByteArray &payload)
{
    QNetworkRequest request(m_postEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (m_config.requestTimeoutMs > 0)
    {
        request.setTransferTimeout(m_config.requestTimeoutMs);
    }
    applyConfiguredHeaders(request);

    QNetworkReply *reply = m_networkAccessManager->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError)
        {
            emit logMessage(QStringLiteral("SSE POST error: %1").arg(reply->errorString()));
        }
        else
        {
            const QByteArray body = reply->readAll();
            if (body.isEmpty() == false)
            {
                emit logMessage(QStringLiteral("SSE POST response: %1")
                                    .arg(QString::fromUtf8(body).left(200)));
            }
        }
        reply->deleteLater();
    });
}

void SseTransport::applyConfiguredHeaders(QNetworkRequest &request) const
{
    for (auto it = m_config.headers.begin(); it != m_config.headers.end(); ++it)
    {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
}

}  // namespace qtmcp
