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
class anthropic_provider_t : public QObject, public iai_provider_t
{
    Q_OBJECT
public:
    /**
     * Creates an Anthropic provider with the default cloud endpoint.
     * @param parent Parent QObject that owns this instance.
     */
    explicit anthropic_provider_t(QObject *parent = nullptr);

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
    QString display_name() const override
    {
        return QStringLiteral("Anthropic API");
    }

    /**
     * Sends a Messages API request to Anthropic.
     * @param messages Conversation history serialized for Anthropic.
     * @param model Remote model identifier.
     * @param temperature Sampling temperature.
     * @param max_tokens Maximum completion token count.
     * @param reasoning_effort Optional effort hint; currently used only as a prompt-level hint.
     * @param callback Receives the final response text or an error.
     * @param stream_callback Receives streamed text deltas; an empty string ends the stream.
     */
    void complete(const QList<chat_message_t> &messages, const QString &model, double temperature,
                  int max_tokens, const QString &reasoning_effort, completion_callback_t callback,
                  stream_callback_t stream_callback = nullptr,
                  progress_callback_t progress_callback = nullptr) override;

    /**
     * Aborts the active network reply, if any.
     */
    void cancel() override;

    /**
     * Sets the base URL for the Anthropic API server.
     * @param url Base URL to use.
     */
    void set_base_url(const QString &url) override
    {
        this->base_url = url;
    }

    /**
     * Sets the API key sent via the x-api-key header.
     * @param key API key.
     */
    void set_api_key(const QString &key) override
    {
        this->api_key = key;
    }

private:
    /** Network access manager shared by provider requests. */
    QNetworkAccessManager nam;

    /** Active reply for the current request, or null when idle. */
    QNetworkReply *current_reply = nullptr;

    /** Base URL for the Anthropic API endpoint. */
    QString base_url = QStringLiteral("https://api.anthropic.com");

    /** API key sent via the x-api-key header. */
    QString api_key;

    /** Partial SSE data waiting for a complete line. */
    QByteArray sse_buffer;

    /** Accumulated streamed text returned to the caller at completion. */
    QString stream_accum;

    /** Usage counters captured from the current streaming response. */
    provider_usage_t stream_usage;
};

}  // namespace qcai2
