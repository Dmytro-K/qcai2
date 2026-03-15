#pragma once

#include "IAIProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

namespace qcai2
{

/**
 * Provider for OpenAI-compatible chat completion endpoints.
 */
class OpenAICompatibleProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    /**
     * Creates a provider with default OpenAI endpoint settings.
     * @param parent Parent QObject that owns this instance.
     */
    explicit OpenAICompatibleProvider(QObject *parent = nullptr);

    /**
     * Returns the provider identifier used in configuration.
     */
    QString id() const override
    {
        return QStringLiteral("openai");
    }

    /**
     * Returns the user-visible provider name.
     */
    QString displayName() const override
    {
        return QStringLiteral("OpenAI-Compatible");
    }

    /**
     * Sends a chat completion request to the configured REST endpoint.
     * @param messages Conversation history in OpenAI chat format.
     * @param model Remote model identifier.
     * @param temperature Sampling temperature.
     * @param maxTokens Maximum completion token count.
     * @param reasoningEffort Optional reasoning hint forwarded when enabled.
     * @param callback Receives the final response text or an error.
     * @param streamCallback Receives SSE deltas; an empty string ends the stream.
     */
    void complete(const QList<ChatMessage> &messages, const QString &model, double temperature,
                  int maxTokens, const QString &reasoningEffort, CompletionCallback callback,
                  StreamCallback streamCallback = nullptr) override;

    /**
     * Aborts the active network reply, if any.
     */
    void cancel() override;

    /**
     * Sets the base URL for the compatible API server.
     * @param url Base URL to use.
     */
    void setBaseUrl(const QString &url) override
    {
        m_baseUrl = url;
    }

    /**
     * Sets the bearer token used for Authorization headers.
     * @param key API key or access token.
     */
    void setApiKey(const QString &key) override
    {
        m_apiKey = key;
    }

    /**
     * Sets extra request headers such as Azure-specific authentication fields.
     * @param headers Additional HTTP headers to send.
     */
    void setExtraHeaders(const QMap<QString, QString> &headers)
    {
        m_extraHeaders = headers;
    }

private:
    /**
     * Processes buffered SSE data for a streaming reply.
     */
    void handleStreamChunk();

    /** Network access manager shared by provider requests. */
    QNetworkAccessManager m_nam;

    /** Active reply for the current request, or null when idle. */
    QNetworkReply *m_currentReply = nullptr;

    /** Base URL for the remote API endpoint. */
    QString m_baseUrl = QStringLiteral("https://api.openai.com");

    /** Bearer token sent in Authorization headers. */
    QString m_apiKey;

    /** Extra raw headers appended to each request. */
    QMap<QString, QString> m_extraHeaders;

    /** Partial SSE data waiting for a complete line. */
    QByteArray m_sseBuffer;

    /** Accumulated streamed text returned to the caller at completion. */
    QString m_streamAccum;

    /** Usage counters captured from the current streaming response. */
    ProviderUsage m_streamUsage;

};

}  // namespace qcai2
