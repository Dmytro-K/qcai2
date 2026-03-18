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
class open_ai_compatible_provider_t : public QObject, public iai_provider_t
{
    Q_OBJECT
public:
    /**
     * Creates a provider with default OpenAI endpoint settings.
     * @param parent Parent QObject that owns this instance.
     */
    explicit open_ai_compatible_provider_t(QObject *parent = nullptr);

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
    QString display_name() const override
    {
        return QStringLiteral("OpenAI-Compatible");
    }

    /**
     * Sends a chat completion request to the configured REST endpoint.
     * @param messages Conversation history in OpenAI chat format.
     * @param model Remote model identifier.
     * @param temperature Sampling temperature.
     * @param max_tokens Maximum completion token count.
     * @param reasoning_effort Optional reasoning hint forwarded when enabled.
     * @param callback Receives the final response text or an error.
     * @param stream_callback Receives SSE deltas; an empty string ends the stream.
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
     * Sets the base URL for the compatible API server.
     * @param url Base URL to use.
     */
    void set_base_url(const QString &url) override
    {
        this->base_url = url;
    }

    /**
     * Sets the bearer token used for Authorization headers.
     * @param key API key or access token.
     */
    void set_api_key(const QString &key) override
    {
        this->api_key = key;
    }

    /**
     * Sets extra request headers such as Azure-specific authentication fields.
     * @param headers Additional HTTP headers to send.
     */
    void set_extra_headers(const QMap<QString, QString> &headers)
    {
        this->extra_headers = headers;
    }

private:
    /**
     * Processes buffered SSE data for a streaming reply.
     */
    void handle_stream_chunk();

    /** Network access manager shared by provider requests. */
    QNetworkAccessManager nam;

    /** Active reply for the current request, or null when idle. */
    QNetworkReply *current_reply = nullptr;

    /** Base URL for the remote API endpoint. */
    QString base_url = QStringLiteral("https://api.openai.com");

    /** Bearer token sent in Authorization headers. */
    QString api_key;

    /** Extra raw headers appended to each request. */
    QMap<QString, QString> extra_headers;

    /** Partial SSE data waiting for a complete line. */
    QByteArray sse_buffer;

    /** Accumulated streamed text returned to the caller at completion. */
    QString stream_accum;

    /** Usage counters captured from the current streaming response. */
    provider_usage_t stream_usage;
};

}  // namespace qcai2
