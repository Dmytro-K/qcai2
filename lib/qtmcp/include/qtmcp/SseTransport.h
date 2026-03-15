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

struct SseTransportConfig
{
    QUrl endpoint;
    QMap<QString, QString> headers;
    int requestTimeoutMs = 30000;
    int reconnectDelayMs = 3000;
};

class SseTransport final : public Transport
{
    Q_OBJECT

public:
    explicit SseTransport(SseTransportConfig config, QObject *parent = nullptr);
    ~SseTransport() override;

    QString transportName() const override;
    State state() const override;
    const SseTransportConfig &config() const;

    void start() override;
    void stop() override;
    bool sendMessage(const QJsonObject &message) override;

private:
    void setState(State state);
    void openSseStream();
    void processStreamReadyRead();
    void processStreamFinished();
    void processSseBuffer(bool flush);
    void handleSseEvent();
    void postMessage(const QByteArray &payload);
    void applyConfiguredHeaders(QNetworkRequest &request) const;

    SseTransportConfig m_config;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QNetworkReply *m_sseReply = nullptr;
    QUrl m_postEndpoint;
    State m_state = State::Disconnected;

    QByteArray m_sseBuffer;
    QByteArray m_currentEventData;
    QString m_currentEventType;
};

}  // namespace qtmcp
