#pragma once

#include "IAIProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

namespace qcai2
{

/**
 * Provider for Anthropic's Messages API.
 */
class AnthropicProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    /**
     * Creates an Anthropic provider with the default cloud endpoint.
     * @param parent Parent QObject that owns this instance.
     */
    explicit AnthropicProvider(QObject *parent = nullptr);

    /**
     * Returns the provider identifier used in configuration.
     */
    QString id() const override
    {
        return QStringLiteral("anthropic");
    }

    /**
     * Returns the user-visible provider name.
     */
    QString displayName() const override
    {
        return QStringLiteral("Anthropic API");
    }

    /**
     * Sends a Messages API request to Anthropic.
     * @param messages Conversation history serialized for Anthropic.
     * @param model Remote model identifier.
     * @param temperature Sampling temperature.
     * @param maxTokens Maximum completion token count.
     * @param reasoningEffort Optional effort hint; currently used only as a prompt-level hint.
     * @param callback Receives the final response text or an error.
     * @param streamCallback Receives streamed text deltas; an empty string ends the stream.
     */
    void complete(const QList<ChatMessage> &messages, const QString &model, double temperature,
                  int maxTokens, const QString &reasoningEffort, CompletionCallback callback,
                  StreamCallback streamCallback = nullptr,
                  ProgressCallback progressCallback = nullptr) override;

    /**
     * Aborts the active network reply, if any.
     */
    void cancel() override;

    /**
     * Sets the base URL for the Anthropic API server.
     * @param url Base URL to use.
     */
    void setBaseUrl(const QString &url) override
    {
        m_baseUrl = url;
    }

    /**
     * Sets the API key sent via the x-api-key header.
     * @param key API key.
     */
    void setApiKey(const QString &key) override
    {
        m_apiKey = key;
    }

private:
    /** Network access manager shared by provider requests. */
    QNetworkAccessManager m_nam;

    /** Active reply for the current request, or null when idle. */
    QNetworkReply *m_currentReply = nullptr;

    /** Base URL for the Anthropic API endpoint. */
    QString m_baseUrl = QStringLiteral("https://api.anthropic.com");

    /** API key sent via the x-api-key header. */
    QString m_apiKey;

    /** Partial SSE data waiting for a complete line. */
    QByteArray m_sseBuffer;

    /** Accumulated streamed text returned to the caller at completion. */
    QString m_streamAccum;

    /** Usage counters captured from the current streaming response. */
    ProviderUsage m_streamUsage;
};

}  // namespace qcai2
