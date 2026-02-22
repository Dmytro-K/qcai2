#include "LocalHttpProvider.h"
#include "../util/Json.h"
#include "../util/Logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>

namespace Qcai2 {

LocalHttpProvider::LocalHttpProvider(QObject *parent) : QObject(parent) {}

void LocalHttpProvider::complete(const QList<ChatMessage> &messages,
                                  const QString &model,
                                  double temperature,
                                  int maxTokens,
                                  CompletionCallback callback,
                                  StreamCallback /*streamCallback*/)
{
    QJsonObject body;

    if (m_simpleMode) {
        // Simple mode: concatenate messages into a single prompt string
        QString prompt;
        for (const auto &m : messages)
            prompt += QStringLiteral("[%1]: %2\n").arg(m.role, m.content);
        body["prompt"] = prompt;
        if (!model.isEmpty()) body["model"] = model;
        body["temperature"] = temperature;
        body["max_tokens"]  = maxTokens;
    } else {
        // OpenAI-compatible format
        QJsonArray msgArr;
        for (const auto &m : messages)
            msgArr.append(m.toJson());
        body["model"]       = model;
        body["messages"]    = msgArr;
        body["temperature"] = temperature;
        body["max_tokens"]  = maxTokens;
    }

    QString urlStr = m_baseUrl;
    if (urlStr.endsWith('/')) urlStr.chop(1);
    urlStr += m_endpointPath;

    QCAI_DEBUG("LocalHTTP", QStringLiteral("POST %1 | model=%2 simple=%3 msgs=%4")
        .arg(urlStr, model, m_simpleMode ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(messages.size()));

    QUrl url(urlStr);
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (!m_apiKey.isEmpty())
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());

    for (auto it = m_customHeaders.begin(); it != m_customHeaders.end(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    m_currentReply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(m_currentReply, &QNetworkReply::finished, this, [this, callback]() {
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        if (!reply) { callback({}, QStringLiteral("Reply was null")); return; }

        if (reply->error() != QNetworkReply::NoError) {
            const auto errBody = QString::fromUtf8(reply->readAll());
            QCAI_ERROR("LocalHTTP", QStringLiteral("HTTP error: %1 — %2")
                .arg(reply->errorString(), errBody.left(300)));
            callback({}, QStringLiteral("HTTP error: %1 — %2")
                             .arg(reply->errorString(), errBody));
            reply->deleteLater();
            return;
        }

        const QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            callback({}, QStringLiteral("JSON parse error: %1").arg(err.errorString()));
            return;
        }

        const QJsonObject root = doc.object();

        // Try OpenAI format first
        QString content = Json::getString(root, QStringLiteral("choices/0/message/content"));
        if (content.isEmpty()) {
            // Try simple format: {"response": "..."}
            content = root.value("response").toString();
        }
        if (content.isEmpty()) {
            // Try {"text": "..."}
            content = root.value("text").toString();
        }
        if (content.isEmpty()) {
            callback({}, QStringLiteral("Could not extract content from: %1")
                             .arg(QString::fromUtf8(data)));
            return;
        }

        callback(content, {});
        QCAI_DEBUG("LocalHTTP", QStringLiteral("Response received, %1 chars").arg(content.length()));
    });
}

void LocalHttpProvider::cancel()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

} // namespace Qcai2
