#pragma once

#include "IAIProvider.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

namespace qcai2
{

/**
 * Provider for local HTTP backends with simple or OpenAI-compatible payloads.
 */
class local_http_provider_t : public QObject, public iai_provider_t
{
    Q_OBJECT
public:
    /**
     * Creates a provider pointed at the default local server URL.
     * @param parent Parent QObject that owns this instance.
     */
    explicit local_http_provider_t(QObject *parent = nullptr);

    /**
     * Returns the provider identifier used in configuration.
     */
    QString id() const override
    {
        return QStringLiteral("local");
    }

    /**
     * Returns the user-visible provider name.
     */
    QString display_name() const override
    {
        return QStringLiteral("Local HTTP");
    }

    /**
     * Sends a completion request to the configured local endpoint.
     * @param messages Conversation history to serialize.
     * @param model Optional backend model identifier.
     * @param temperature Sampling temperature.
     * @param max_tokens Maximum completion token count.
     * @param reasoning_effort Optional reasoning hint for compatible backends.
     * @param callback Receives the response text or an error.
     * @param stream_callback Unused because this provider currently replies once.
     */
    void complete(const QList<chat_message_t> &messages, const QString &model, double temperature,
                  int max_tokens, const QString &reasoning_effort, completion_callback_t callback,
                  stream_callback_t stream_callback = nullptr,
                  progress_callback_t progress_callback = nullptr) override;

    /**
     * Aborts the active HTTP request, if any.
     */
    void cancel() override;

    /**
     * Sets the server base URL.
     * @param url Base URL to use.
     */
    void set_base_url(const QString &url) override
    {
        this->base_url = url;
    }

    /**
     * Sets the bearer token sent with requests.
     * @param key API key or access token.
     */
    void set_api_key(const QString &key) override
    {
        this->api_key = key;
    }

    /**
     * Sets the endpoint path appended to the base URL.
     * @param path Path value.
     */
    void set_endpoint_path(const QString &path)
    {
        this->endpoint_path = path;
    }

    /**
     * Selects simple prompt mode instead of OpenAI chat message mode.
     * @param simple True to send one prompt string instead of chat messages.
     */
    void set_simple_mode(bool simple)
    {
        this->simple_mode = simple;
    }

    /**
     * Sets additional raw HTTP headers for each request.
     * @param h Additional raw HTTP headers to include with each request.
     */
    void set_custom_headers(const QMap<QString, QString> &h)
    {
        this->custom_headers = h;
    }

private:
    /** Network access manager used for local requests. */
    QNetworkAccessManager nam;

    /** Active reply for the current request, or null when idle. */
    QNetworkReply *current_reply = nullptr;

    /** Base URL of the local server. */
    QString base_url = QStringLiteral("http://localhost:8080");

    /** Optional bearer token for authenticated local endpoints. */
    QString api_key;

    /** Request path appended to the base URL. */
    QString endpoint_path = QStringLiteral("/v1/chat/completions");

    /** True when the backend expects a single prompt string. */
    bool simple_mode = false;

    /** Additional request headers forwarded to the backend. */
    QMap<QString, QString> custom_headers;
};

}  // namespace qcai2
