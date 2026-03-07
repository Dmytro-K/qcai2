#pragma once

#include "IAIProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

namespace qcai2
{

// Provider that talks to OpenAI-compatible REST endpoints.
// Works with OpenAI, Azure OpenAI, vLLM, LMStudio, text-generation-webui, etc.
class OpenAICompatibleProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    explicit OpenAICompatibleProvider(QObject *parent = nullptr);

    QString id() const override
    {
        return QStringLiteral("openai");
    }
    QString displayName() const override
    {
        return QStringLiteral("OpenAI-Compatible");
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

    // Custom headers (e.g. for Azure)
    void setExtraHeaders(const QMap<QString, QString> &headers)
    {
        m_extraHeaders = headers;
    }

private:
    void handleStreamChunk();

    QNetworkAccessManager m_nam;
    QNetworkReply *m_currentReply = nullptr;
    QString m_baseUrl = QStringLiteral("https://api.openai.com");
    QString m_apiKey;
    QMap<QString, QString> m_extraHeaders;
    QByteArray m_sseBuffer;
    QString m_streamAccum;
};

}  // namespace qcai2
