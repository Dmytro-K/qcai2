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
class LocalHttpProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    /**
     * Creates a provider pointed at the default local server URL.
     * @param parent Parent QObject that owns this instance.
     */
    explicit LocalHttpProvider(QObject *parent = nullptr);

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
    QString displayName() const override
    {
        return QStringLiteral("Local HTTP");
    }

    /**
     * Sends a completion request to the configured local endpoint.
     * @param messages Conversation history to serialize.
     * @param model Optional backend model identifier.
     * @param temperature Sampling temperature.
     * @param maxTokens Maximum completion token count.
     * @param reasoningEffort Optional reasoning hint for compatible backends.
     * @param callback Receives the response text or an error.
     * @param streamCallback Unused because this provider currently replies once.
     */
    void complete(const QList<ChatMessage> &messages, const QString &model, double temperature,
                  int maxTokens, const QString &reasoningEffort, CompletionCallback callback,
                  StreamCallback streamCallback = nullptr) override;

    /**
     * Aborts the active HTTP request, if any.
     */
    void cancel() override;

    /**
     * Sets the server base URL.
     * @param url Base URL to use.
     */
    void setBaseUrl(const QString &url) override
    {
        m_baseUrl = url;
    }

    /**
     * Sets the bearer token sent with requests.
     * @param key API key or access token.
     */
    void setApiKey(const QString &key) override
    {
        m_apiKey = key;
    }

    /**
     * Sets the endpoint path appended to the base URL.
     * @param path Path value.
     */
    void setEndpointPath(const QString &path)
    {
        m_endpointPath = path;
    }

    /**
     * Selects simple prompt mode instead of OpenAI chat message mode.
     * @param simple True to send one prompt string instead of chat messages.
     */
    void setSimpleMode(bool simple)
    {
        m_simpleMode = simple;
    }

    /**
     * Sets additional raw HTTP headers for each request.
     * @param h Additional raw HTTP headers to include with each request.
     */
    void setCustomHeaders(const QMap<QString, QString> &h)
    {
        m_customHeaders = h;
    }

private:
    /** Network access manager used for local requests. */
    QNetworkAccessManager m_nam;

    /** Active reply for the current request, or null when idle. */
    QNetworkReply *m_currentReply = nullptr;

    /** Base URL of the local server. */
    QString m_baseUrl = QStringLiteral("http://localhost:8080");

    /** Optional bearer token for authenticated local endpoints. */
    QString m_apiKey;

    /** Request path appended to the base URL. */
    QString m_endpointPath = QStringLiteral("/v1/chat/completions");

    /** True when the backend expects a single prompt string. */
    bool m_simpleMode = false;

    /** Additional request headers forwarded to the backend. */
    QMap<QString, QString> m_customHeaders;

};

}  // namespace qcai2
