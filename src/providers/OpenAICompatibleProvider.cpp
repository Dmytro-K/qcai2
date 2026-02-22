#include "OpenAICompatibleProvider.h"
#include "../util/Json.h"
#include "../util/Logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>

namespace Qcai2 {

OpenAICompatibleProvider::OpenAICompatibleProvider(QObject *parent)
    : QObject(parent)
{}

void OpenAICompatibleProvider::complete(const QList<ChatMessage> &messages,
                                         const QString &model,
                                         double temperature,
                                         int maxTokens,
                                         CompletionCallback callback,
                                         StreamCallback streamCallback)
{
    // Build request body
    QJsonArray msgArr;
    for (const auto &m : messages)
        msgArr.append(m.toJson());

    QJsonObject body;
    body[QStringLiteral("model")]       = model;
    body[QStringLiteral("messages")]    = msgArr;
    body[QStringLiteral("temperature")] = temperature;
    body[QStringLiteral("max_tokens")]  = maxTokens;

    const bool streaming = (streamCallback != nullptr);
    if (streaming)
        body[QStringLiteral("stream")] = true;

    // Build URL
    QString urlStr = m_baseUrl;
    if (!urlStr.endsWith(QLatin1Char('/'))) urlStr += QLatin1Char('/');
    urlStr += QStringLiteral("v1/chat/completions");

    QCAI_DEBUG("OpenAI", QStringLiteral("POST %1 | model=%2 temp=%3 maxTok=%4 stream=%5 msgs=%6")
        .arg(urlStr, model).arg(temperature).arg(maxTokens)
        .arg(streaming ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(messages.size()));

    QUrl url(urlStr);
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (!m_apiKey.isEmpty())
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());

    for (auto it = m_extraHeaders.begin(); it != m_extraHeaders.end(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    m_sseBuffer.clear();
    m_streamAccum.clear();

    m_currentReply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (streaming) {
        // Stream mode: process SSE chunks as they arrive
        connect(m_currentReply, &QNetworkReply::readyRead, this, [this, streamCallback]() {
            m_sseBuffer.append(m_currentReply->readAll());

            // Process complete SSE lines
            while (true) {
                qsizetype idx = m_sseBuffer.indexOf('\n');
                if (idx < 0) break;

                QByteArray line = m_sseBuffer.left(idx).trimmed();
                m_sseBuffer.remove(0, idx + 1);

                if (line.isEmpty()) continue;

                // SSE format: "data: {...}" or "data: [DONE]"
                if (!line.startsWith("data: ")) continue;
                QByteArray payload = line.mid(6);

                if (payload == "[DONE]") continue;

                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
                if (err.error != QJsonParseError::NoError) continue;

                // Extract delta content: choices[0].delta.content
                QJsonObject obj = doc.object();
                QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty()) continue;

                const QJsonObject deltaObj = choices[0].toObject().value(QStringLiteral("delta")).toObject();

                const QString reasoningDelta = !deltaObj.value(QStringLiteral("reasoning_content")).toString().isEmpty()
                                                   ? deltaObj.value(QStringLiteral("reasoning_content")).toString()
                                                   : deltaObj.value(QStringLiteral("reasoning")).toString();
                if (!reasoningDelta.isEmpty())
                    streamCallback(reasoningDelta);

                const QString delta = deltaObj.value(QStringLiteral("content")).toString();
                if (!delta.isEmpty()) {
                    m_streamAccum += delta;
                    streamCallback(delta);
                }
            }
        });

        connect(m_currentReply, &QNetworkReply::finished, this, [this, callback, streamCallback]() {
            QNetworkReply *reply = m_currentReply;
            m_currentReply = nullptr;

            if (!reply) { callback({}, QStringLiteral("Reply was null")); return; }

            if (reply->error() != QNetworkReply::NoError
                && reply->error() != QNetworkReply::OperationCanceledError) {
                const QString errBody = QString::fromUtf8(reply->readAll());
                QCAI_ERROR("OpenAI", QStringLiteral("HTTP error (streaming): %1 — %2")
                    .arg(reply->errorString(), errBody.left(300)));
                callback({}, QStringLiteral("HTTP error: %1 — %2").arg(reply->errorString(), errBody));
                reply->deleteLater();
                return;
            }

            reply->deleteLater();
            QCAI_DEBUG("OpenAI", QStringLiteral("Stream complete, accumulated %1 chars").arg(m_streamAccum.length()));
            streamCallback({}); // signal stream end
            callback(m_streamAccum, {});
            m_streamAccum.clear();
            m_sseBuffer.clear();
        });
    } else {
        // Non-streaming mode (original)
        connect(m_currentReply, &QNetworkReply::finished, this, [this, callback]() {
            QNetworkReply *reply = m_currentReply;
            m_currentReply = nullptr;

            if (!reply) { callback({}, QStringLiteral("Reply was null")); return; }

            if (reply->error() != QNetworkReply::NoError) {
                const QString errBody = QString::fromUtf8(reply->readAll());
                QCAI_ERROR("OpenAI", QStringLiteral("HTTP error: %1 — %2")
                    .arg(reply->errorString(), errBody.left(300)));
                callback({}, QStringLiteral("HTTP error: %1 — %2").arg(reply->errorString(), errBody));
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

            const QString content = Json::getString(doc.object(),
                                                    QStringLiteral("choices/0/message/content"));
            if (content.isEmpty()) {
                QCAI_WARN("OpenAI", QStringLiteral("No content in response: %1").arg(QString::fromUtf8(data).left(200)));
                callback({}, QStringLiteral("No content in response: %1").arg(QString::fromUtf8(data)));
                return;
            }

            QCAI_DEBUG("OpenAI", QStringLiteral("Response received, %1 chars").arg(content.length()));
            callback(content, {});
        });
    }
}

void OpenAICompatibleProvider::cancel()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

} // namespace Qcai2
