/*! Declares the legacy SSE transport implementation for the Qt MCP client. */
#pragma once

#include <qtmcp/Transport.h>

#include <QByteArray>
#include <QMap>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace qtmcp
{

struct sse_transport_config_t
{
    QUrl endpoint;
    QMap<QString, QString> headers;
    int request_timeout_ms = 30000;
    int reconnect_delay_ms = 3000;
};

class sse_transport_t final : public transport_t
{
    Q_OBJECT

public:
    explicit sse_transport_t(sse_transport_config_t config, QObject *parent = nullptr);
    ~sse_transport_t() override;

    QString transport_name() const override;
    state_t state() const override;
    const sse_transport_config_t &config() const;

    void start() override;
    void stop() override;
    bool send_message(const QJsonObject &message) override;

private:
    void set_state(state_t state);
    void open_sse_stream();
    void process_stream_ready_read();
    void process_stream_finished();
    void process_sse_buffer(bool flush);
    void handle_sse_event();
    void post_message(const QByteArray &payload);
    void apply_configured_headers(QNetworkRequest &request) const;

    sse_transport_config_t transport_config;
    QNetworkAccessManager *network_access_manager = nullptr;
    QNetworkReply *sse_reply = nullptr;
    QUrl post_endpoint;
    state_t transport_state = state_t::DISCONNECTED;

    QByteArray sse_buffer;
    QByteArray current_event_data;
    QString current_event_type;
};

}  // namespace qtmcp
