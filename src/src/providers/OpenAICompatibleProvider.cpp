#include "OpenAICompatibleProvider.h"
#include "../util/Json.h"
#include "../util/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>

#include <memory>

namespace qcai2
{

/**
 * Creates an OpenAI-compatible provider instance.
 * @param parent Parent QObject that owns this instance.
 */
OpenAICompatibleProvider::OpenAICompatibleProvider(QObject *parent) : QObject(parent)
{
}

/**
 * Serializes a chat request and sends it to the configured endpoint.
 * @param messages Conversation history to send.
 * @param model Remote model identifier.
 * @param temperature Sampling temperature.
 * @param maxTokens Maximum completion token count.
 * @param reasoningEffort Optional reasoning hint forwarded when enabled.
 * @param callback Receives the final response text or an error.
 * @param streamCallback Receives streamed SSE deltas when streaming is enabled.
 */
void OpenAICompatibleProvider::complete(const QList<ChatMessage> &messages, const QString &model,
                                        double temperature, int maxTokens,
                                        const QString &reasoningEffort,
                                        CompletionCallback callback, StreamCallback streamCallback,
                                        ProgressCallback progressCallback)
{
    if (progressCallback != nullptr)
    {
        progressCallback({ProviderRawEventKind::RequestStarted,
                          id(),
                          QStringLiteral("request.started"),
                          {},
                          {}});
    }

    // Build request body
    QJsonArray msgArr;
    for (const auto &m : messages)
    {
        msgArr.append(m.toJson());
    }

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("messages")] = msgArr;
    body[QStringLiteral("temperature")] = temperature;
    body[QStringLiteral("max_tokens")] = maxTokens;

    if (((!reasoningEffort.isEmpty() && reasoningEffort != QStringLiteral("off")) == true))
    {
        body[QStringLiteral("reasoning_effort")] = reasoningEffort;
    }

    const bool streaming = (streamCallback != nullptr);
    if (streaming == true)
    {
        body[QStringLiteral("stream")] = true;
        body[QStringLiteral("stream_options")] =
            QJsonObject{{QStringLiteral("include_usage"), true}};
    }

    // Build URL
    QString urlStr = m_baseUrl;
    if (((urlStr.endsWith(QLatin1Char('/'))) == false))
    {
        urlStr += QLatin1Char('/');
    }
    urlStr += QStringLiteral("v1/chat/completions");

    QCAI_DEBUG("OpenAI", QStringLiteral("POST %1 | model=%2 temp=%3 maxTok=%4 stream=%5 msgs=%6")
                             .arg(urlStr, model)
                             .arg(temperature)
                             .arg(maxTokens)
                             .arg(streaming ? QStringLiteral("yes") : QStringLiteral("no"))
                             .arg(messages.size()));

    QUrl url(urlStr);
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (((!m_apiKey.isEmpty()) == true))
    {
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());
    }

    for (auto it = m_extraHeaders.begin(); ((it != m_extraHeaders.end()) == true); ++it)
    {
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    if (streaming == false)
    {
        req.setTransferTimeout(15000);
        req.setRawHeader("Connection", "close");
    }

    m_sseBuffer.clear();
    m_streamAccum.clear();
    m_streamUsage = {};

    m_currentReply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (streaming == true)
    {
        const auto activeTools = std::make_shared<QSet<QString>>();

        // Stream mode: process SSE chunks as they arrive
        connect(
            m_currentReply, &QNetworkReply::readyRead, this,
            [this, streamCallback, progressCallback, activeTools]() {
                m_sseBuffer.append(m_currentReply->readAll());

                // Process complete SSE lines
                while (true == true)
                {
                    qsizetype idx = m_sseBuffer.indexOf('\n');
                    if (idx < 0)
                    {
                        break;
                    }

                    QByteArray line = m_sseBuffer.left(idx).trimmed();
                    m_sseBuffer.remove(0, idx + 1);

                    if (line.isEmpty() == true)
                    {
                        continue;
                    }

                    // SSE format: "data: {...}" or "data: [DONE]"
                    if (line.startsWith("data: ") == false)
                    {
                        continue;
                    }
                    QByteArray payload = line.mid(6);

                    if (((payload == "[DONE]") == true))
                    {
                        continue;
                    }

                    QJsonParseError err;
                    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
                    if (((err.error != QJsonParseError::NoError) == true))
                    {
                        continue;
                    }

                    QJsonObject obj = doc.object();
                    const ProviderUsage usage = providerUsageFromResponseObject(obj);
                    if (usage.hasAny() == true)
                    {
                        m_streamUsage = usage;
                    }

                    // Extract delta content: choices[0].delta.content
                    QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
                    if (choices.isEmpty() == true)
                    {
                        continue;
                    }

                    const QJsonObject deltaObj =
                        choices[0].toObject().value(QStringLiteral("delta")).toObject();

                    const QString reasoningDelta =
                        !deltaObj.value(QStringLiteral("reasoning_content")).toString().isEmpty()
                            ? deltaObj.value(QStringLiteral("reasoning_content")).toString()
                            : deltaObj.value(QStringLiteral("reasoning")).toString();
                    if (reasoningDelta.isEmpty() == false)
                    {
                        if (progressCallback != nullptr)
                        {
                            progressCallback({ProviderRawEventKind::ReasoningDelta,
                                              id(),
                                              QStringLiteral("response.reasoning.delta"),
                                              {},
                                              reasoningDelta});
                        }
                        streamCallback(reasoningDelta);
                    }

                    const QJsonArray toolCalls =
                        deltaObj.value(QStringLiteral("tool_calls")).toArray();
                    for (const QJsonValue &toolCallValue : toolCalls)
                    {
                        const QJsonObject toolCall = toolCallValue.toObject();
                        const QString toolName = toolCall.value(QStringLiteral("function"))
                                                     .toObject()
                                                     .value(QStringLiteral("name"))
                                                     .toString()
                                                     .trimmed();
                        if (!toolName.isEmpty() && progressCallback != nullptr)
                        {
                            progressCallback({ProviderRawEventKind::ToolStarted,
                                              id(),
                                              QStringLiteral("response.tool_call.delta"),
                                              toolName,
                                              {}});
                        }
                        if (!toolName.isEmpty())
                        {
                            activeTools->insert(toolName);
                        }
                    }

                    const QString delta = deltaObj.value(QStringLiteral("content")).toString();
                    if (delta.isEmpty() == false)
                    {
                        m_streamAccum += delta;
                        if (progressCallback != nullptr)
                        {
                            progressCallback({ProviderRawEventKind::MessageDelta,
                                              id(),
                                              QStringLiteral("assistant.message_delta"),
                                              {},
                                              delta});
                        }
                        streamCallback(delta);
                    }

                    const QString finishReason =
                        choices[0].toObject().value(QStringLiteral("finish_reason")).toString();
                    if (finishReason == QStringLiteral("tool_calls") &&
                        progressCallback != nullptr)
                    {
                        for (const QString &toolName : std::as_const(*activeTools))
                        {
                            progressCallback({ProviderRawEventKind::ToolCompleted,
                                              id(),
                                              QStringLiteral("response.tool_call.completed"),
                                              toolName,
                                              {}});
                        }
                        activeTools->clear();
                    }
                }
            });

        connect(
            m_currentReply, &QNetworkReply::finished, this,
            [this, callback, streamCallback, progressCallback, activeTools]() {
                QNetworkReply *reply = m_currentReply;
                m_currentReply = nullptr;

                if (((reply == nullptr) == true))
                {
                    callback({}, QStringLiteral("Reply was null"), {});
                    return;
                }

                if (((reply->error() != QNetworkReply::NoError &&
                      reply->error() != QNetworkReply::OperationCanceledError) == true))
                {
                    const QString errBody = QString::fromUtf8(reply->readAll());
                    if (progressCallback != nullptr)
                    {
                        progressCallback({ProviderRawEventKind::Error,
                                          id(),
                                          QStringLiteral("response.error"),
                                          {},
                                          reply->errorString()});
                    }
                    QCAI_ERROR("OpenAI", QStringLiteral("HTTP error (streaming): %1 — %2")
                                             .arg(reply->errorString(), errBody.left(300)));
                    callback(
                        {},
                        QStringLiteral("HTTP error: %1 — %2").arg(reply->errorString(), errBody),
                        {});
                    reply->deleteLater();
                    return;
                }

                if (progressCallback != nullptr)
                {
                    for (const QString &toolName : std::as_const(*activeTools))
                    {
                        progressCallback({ProviderRawEventKind::ToolCompleted,
                                          id(),
                                          QStringLiteral("response.tool_call.completed"),
                                          toolName,
                                          {}});
                    }
                    progressCallback({ProviderRawEventKind::ResponseCompleted,
                                      id(),
                                      QStringLiteral("response.completed"),
                                      {},
                                      {}});
                }
                const ProviderUsage usage = m_streamUsage;
                reply->deleteLater();
                QCAI_DEBUG("OpenAI", QStringLiteral("Stream complete, accumulated %1 chars")
                                         .arg(m_streamAccum.length()));
                streamCallback({});  // signal stream end
                callback(m_streamAccum, {}, usage);
                m_streamAccum.clear();
                m_sseBuffer.clear();
                m_streamUsage = {};
            });
    }
    else
    {
        // Non-streaming mode (original)
        connect(
            m_currentReply, &QNetworkReply::finished, this, [this, callback, progressCallback]() {
                QNetworkReply *reply = m_currentReply;
                m_currentReply = nullptr;

                if (!reply)
                {
                    callback({}, QStringLiteral("Reply was null"), {});
                    return;
                }

                if (reply->error() != QNetworkReply::NoError)
                {
                    const QString errBody = QString::fromUtf8(reply->readAll());
                    if (progressCallback != nullptr)
                    {
                        progressCallback({ProviderRawEventKind::Error,
                                          id(),
                                          QStringLiteral("response.error"),
                                          {},
                                          reply->errorString()});
                    }
                    QCAI_ERROR("OpenAI", QStringLiteral("HTTP error: %1 — %2")
                                             .arg(reply->errorString(), errBody.left(300)));
                    callback(
                        {},
                        QStringLiteral("HTTP error: %1 — %2").arg(reply->errorString(), errBody),
                        {});
                    reply->deleteLater();
                    return;
                }

                const QByteArray data = reply->readAll();
                reply->deleteLater();

                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error != QJsonParseError::NoError)
                {
                    callback({}, QStringLiteral("JSON parse error: %1").arg(err.errorString()),
                             {});
                    return;
                }

                const ProviderUsage usage = providerUsageFromResponseObject(doc.object());
                const QString content =
                    Json::getString(doc.object(), QStringLiteral("choices/0/message/content"));
                if (content.isEmpty())
                {
                    QCAI_WARN("OpenAI", QStringLiteral("No content in response: %1")
                                            .arg(QString::fromUtf8(data).left(200)));
                    callback(
                        {},
                        QStringLiteral("No content in response: %1").arg(QString::fromUtf8(data)),
                        {});
                    return;
                }

                QCAI_DEBUG("OpenAI",
                           QStringLiteral("Response received, %1 chars").arg(content.length()));
                if (progressCallback != nullptr)
                {
                    progressCallback({ProviderRawEventKind::ResponseCompleted,
                                      id(),
                                      QStringLiteral("response.completed"),
                                      {},
                                      {}});
                }
                callback(content, {}, usage);
            });
    }
}

/**
 * Aborts the active network reply, if one is running.
 */
void OpenAICompatibleProvider::cancel()
{
    if (m_currentReply != nullptr)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

}  // namespace qcai2
