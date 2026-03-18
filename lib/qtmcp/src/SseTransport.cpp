/*! Implements the legacy SSE transport for the Qt MCP client.
 *
 * The legacy MCP SSE transport (protocol version 2024-11-05) works as follows:
 *  1. client_t opens a persistent GET connection to the SSE endpoint.
 *  2. Server sends an `event: endpoint` with the POST URL for client messages.
 *  3. client_t sends JSON-RPC messages via HTTP POST to that URL.
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

sse_transport_t::sse_transport_t(sse_transport_config_t config, QObject *parent)
    : transport_t(parent), transport_config(std::move(config)),
      network_access_manager(new QNetworkAccessManager(this))
{
}

sse_transport_t::~sse_transport_t()
{
    stop();
}

QString sse_transport_t::transport_name() const
{
    return QStringLiteral("sse");
}

transport_t::state_t sse_transport_t::state() const
{
    return this->transport_state;
}

const sse_transport_config_t &sse_transport_t::config() const
{
    return this->transport_config;
}

void sse_transport_t::start()
{
    if (this->transport_state != state_t::DISCONNECTED)
    {
        return;
    }

    const QString scheme = this->transport_config.endpoint.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
    {
        emit error_occurred(
            QStringLiteral("SSE transport requires http/https URL, got: %1").arg(scheme));
        return;
    }

    this->set_state(state_t::STARTING);
    this->post_endpoint.clear();
    this->open_sse_stream();
}

void sse_transport_t::stop()
{
    if (this->transport_state == state_t::DISCONNECTED)
    {
        return;
    }

    this->set_state(state_t::STOPPING);

    if (this->sse_reply != nullptr)
    {
        this->sse_reply->abort();
        this->sse_reply->deleteLater();
        this->sse_reply = nullptr;
    }

    this->sse_buffer.clear();
    this->current_event_data.clear();
    this->current_event_type.clear();
    this->post_endpoint.clear();

    this->set_state(state_t::DISCONNECTED);
    emit stopped();
}

bool sse_transport_t::send_message(const QJsonObject &message)
{
    if (this->transport_state != state_t::CONNECTED)
    {
        emit error_occurred(QStringLiteral("Cannot send message: SSE transport not connected."));
        return false;
    }

    if (this->post_endpoint.isEmpty() == true)
    {
        emit error_occurred(
            QStringLiteral("Cannot send message: server has not provided a POST endpoint yet."));
        return false;
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    emit log_message(QStringLiteral("sse -> %1").arg(QString::fromUtf8(payload)));
    this->post_message(payload);
    return true;
}

void sse_transport_t::set_state(state_t state)
{
    if (this->transport_state == state)
    {
        return;
    }

    this->transport_state = state;
    QString state_label = QStringLiteral("unknown");
    switch (this->transport_state)
    {
        case state_t::DISCONNECTED:
            state_label = QStringLiteral("disconnected");
            break;
        case state_t::STARTING:
            state_label = QStringLiteral("starting");
            break;
        case state_t::CONNECTED:
            state_label = QStringLiteral("connected");
            break;
        case state_t::STOPPING:
            state_label = QStringLiteral("stopping");
            break;
    }
    emit log_message(QStringLiteral("sse state changed: %1").arg(state_label));
    emit state_changed(this->transport_state);
}

void sse_transport_t::open_sse_stream()
{
    QNetworkRequest request(this->transport_config.endpoint);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    // No transfer timeout — the stream stays open indefinitely
    request.setTransferTimeout(0);
    this->apply_configured_headers(request);

    emit log_message(
        QStringLiteral("sse opening stream: %1").arg(this->transport_config.endpoint.toString()));
    this->sse_reply = this->network_access_manager->get(request);

    connect(this->sse_reply, &QNetworkReply::readyRead, this,
            &sse_transport_t::process_stream_ready_read);
    connect(this->sse_reply, &QNetworkReply::finished, this,
            &sse_transport_t::process_stream_finished);
    connect(this->sse_reply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError code) {
                emit log_message(QStringLiteral("SSE stream error: code=%1 text=%2")
                                     .arg(static_cast<int>(code))
                                     .arg(this->sse_reply != nullptr
                                              ? this->sse_reply->errorString()
                                              : QStringLiteral("(null reply)")));
            });
}

void sse_transport_t::process_stream_ready_read()
{
    if (this->sse_reply == nullptr)
    {
        return;
    }

    const QByteArray chunk = this->sse_reply->readAll();
    if (chunk.isEmpty() == true)
    {
        return;
    }

    this->sse_buffer += chunk;
    this->process_sse_buffer(false);
}

void sse_transport_t::process_stream_finished()
{
    if (this->sse_reply == nullptr)
    {
        return;
    }

    // Flush remaining buffer
    if (this->sse_buffer.isEmpty() == false)
    {
        this->process_sse_buffer(true);
    }

    const int status_code =
        this->sse_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error_string = this->sse_reply->errorString();

    this->sse_reply->deleteLater();
    this->sse_reply = nullptr;

    if (this->transport_state == state_t::STOPPING ||
        this->transport_state == state_t::DISCONNECTED)
    {
        return;
    }

    emit log_message(QStringLiteral("SSE stream closed: status=%1 error=%2")
                         .arg(status_code)
                         .arg(error_string));

    // Reconnect after delay
    if (this->transport_config.reconnect_delay_ms > 0)
    {
        emit log_message(QStringLiteral("SSE reconnecting in %1 ms...")
                             .arg(this->transport_config.reconnect_delay_ms));
        QTimer::singleShot(this->transport_config.reconnect_delay_ms, this, [this]() {
            if (this->transport_state != state_t::STOPPING &&
                this->transport_state != state_t::DISCONNECTED)
            {
                this->sse_buffer.clear();
                this->current_event_data.clear();
                this->current_event_type.clear();
                this->open_sse_stream();
            }
        });
    }
    else
    {
        emit error_occurred(QStringLiteral("SSE stream closed unexpectedly."));
        this->set_state(state_t::DISCONNECTED);
        emit stopped();
    }
}

void sse_transport_t::process_sse_buffer(bool flush)
{
    while (true)
    {
        const qsizetype newline_index = this->sse_buffer.indexOf('\n');
        if (newline_index < 0)
        {
            break;
        }

        QByteArray line = this->sse_buffer.left(newline_index);
        this->sse_buffer.remove(0, newline_index + 1);
        if (line.endsWith('\r') == true)
        {
            line.chop(1);
        }

        // Empty line = end of event
        if (line.isEmpty() == true)
        {
            this->handle_sse_event();
            continue;
        }

        // Comment line
        if (line.startsWith(':') == true)
        {
            continue;
        }

        if (line.startsWith("event:") == true)
        {
            this->current_event_type = QString::fromUtf8(line.mid(6)).trimmed();
            continue;
        }

        if (line.startsWith("data:") == true)
        {
            QByteArray data = line.mid(5);
            if (data.startsWith(' ') == true)
            {
                data.remove(0, 1);
            }
            if (this->current_event_data.isEmpty() == false)
            {
                this->current_event_data.append('\n');
            }
            this->current_event_data.append(data);
        }
    }

    if (flush && (!this->current_event_data.isEmpty() || !this->current_event_type.isEmpty()))
    {
        this->handle_sse_event();
    }
}

void sse_transport_t::handle_sse_event()
{
    const QByteArray data = this->current_event_data;
    const QString event_type = this->current_event_type;
    this->current_event_data.clear();
    this->current_event_type.clear();

    if (data.trimmed().isEmpty() == true)
    {
        return;
    }

    emit log_message(QStringLiteral("sse event `%1` <- %2")
                         .arg(event_type.isEmpty() ? QStringLiteral("message") : event_type,
                              QString::fromUtf8(data).left(200)));

    // The server sends "endpoint" event with the POST URL
    if (event_type == QStringLiteral("endpoint"))
    {
        const QString endpoint_path = QString::fromUtf8(data).trimmed();
        // Resolve relative URL against the SSE endpoint
        this->post_endpoint = this->transport_config.endpoint.resolved(QUrl(endpoint_path));
        emit log_message(
            QStringLiteral("SSE received POST endpoint: %1").arg(this->post_endpoint.toString()));

        if (this->transport_state == state_t::STARTING)
        {
            this->set_state(state_t::CONNECTED);
            emit started();
        }
        return;
    }

    // Parse JSON-RPC message from the stream
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        emit error_occurred(QStringLiteral("Failed to parse MCP SSE event JSON: %1")
                                .arg(parse_error.errorString()));
        return;
    }

    if (document.isObject() == true)
    {
        emit message_received(document.object());
    }
    else if (document.isArray() == true)
    {
        const auto array = document.array();
        for (const auto &value : array)
        {
            if (value.isObject() == true)
            {
                emit message_received(value.toObject());
            }
        }
    }
}

void sse_transport_t::post_message(const QByteArray &payload)
{
    QNetworkRequest request(this->post_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (this->transport_config.request_timeout_ms > 0)
    {
        request.setTransferTimeout(this->transport_config.request_timeout_ms);
    }
    this->apply_configured_headers(request);

    QNetworkReply *reply = this->network_access_manager->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError)
        {
            emit log_message(QStringLiteral("SSE POST error: %1").arg(reply->errorString()));
        }
        else
        {
            const QByteArray body = reply->readAll();
            if (body.isEmpty() == false)
            {
                emit log_message(QStringLiteral("SSE POST response: %1")
                                     .arg(QString::fromUtf8(body).left(200)));
            }
        }
        reply->deleteLater();
    });
}

void sse_transport_t::apply_configured_headers(QNetworkRequest &request) const
{
    for (auto it = this->transport_config.headers.begin();
         it != this->transport_config.headers.end(); ++it)
    {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
}

}  // namespace qtmcp
