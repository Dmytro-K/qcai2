#pragma once

#include "IAIProvider.h"

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

namespace qcai2
{

// Provider for arbitrary local HTTP endpoints.
// Supports OpenAI-compatible schema or a simple {prompt, response} schema.
class LocalHttpProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    explicit LocalHttpProvider(QObject *parent = nullptr);

    QString id() const override
    {
        return QStringLiteral("local");
    }
    QString displayName() const override
    {
        return QStringLiteral("Local HTTP");
    }

    void complete(const QList<ChatMessage> &messages, const QString &model, double temperature,
                  int maxTokens, const QString &reasoningEffort, CompletionCallback callback,
                  StreamCallback streamCallback = nullptr) override;

    void cancel() override;

    void setBaseUrl(const QString &url) override
    {
        m_baseUrl = url;
    }
    void setApiKey(const QString &key) override
    {
        m_apiKey = key;
    }

    // Custom endpoint path (default: "/v1/chat/completions")
    void setEndpointPath(const QString &path)
    {
        m_endpointPath = path;
    }

    // If true, use simple {prompt} format instead of OpenAI chat format
    void setSimpleMode(bool simple)
    {
        m_simpleMode = simple;
    }

    // Custom request headers
    void setCustomHeaders(const QMap<QString, QString> &h)
    {
        m_customHeaders = h;
    }

private:
    QNetworkAccessManager m_nam;
    QNetworkReply *m_currentReply = nullptr;
    QString m_baseUrl = QStringLiteral("http://localhost:8080");
    QString m_apiKey;
    QString m_endpointPath = QStringLiteral("/v1/chat/completions");
    bool m_simpleMode = false;
    QMap<QString, QString> m_customHeaders;
};

}  // namespace qcai2
