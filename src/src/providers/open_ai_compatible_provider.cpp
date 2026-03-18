#include "open_ai_compatible_provider.h"
#include "../util/json.h"
#include "../util/logger.h"

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
open_ai_compatible_provider_t::open_ai_compatible_provider_t(QObject *parent) : QObject(parent)
{
}

/**
 * Serializes a chat request and sends it to the configured endpoint.
 * @param messages Conversation history to send.
 * @param model Remote model identifier.
 * @param temperature Sampling temperature.
 * @param max_tokens Maximum completion token count.
 * @param reasoning_effort Optional reasoning hint forwarded when enabled.
 * @param callback Receives the final response text or an error.
 * @param stream_callback Receives streamed SSE deltas when streaming is enabled.
 */
void open_ai_compatible_provider_t::complete(const QList<chat_message_t> &messages,
                                             const QString &model, double temperature,
                                             int max_tokens, const QString &reasoning_effort,
                                             completion_callback_t callback,
                                             stream_callback_t stream_callback,
                                             progress_callback_t progress_callback)
{
    if (progress_callback != nullptr)
    {
        progress_callback({provider_raw_event_kind_t::REQUEST_STARTED,
                           this->id(),
                           QStringLiteral("request.started"),
                           {},
                           {}});
    }

    // Build request body
    QJsonArray msgArr;
    for (const auto &m : messages)
    {
        msgArr.append(m.to_json());
    }

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("messages")] = msgArr;
    body[QStringLiteral("temperature")] = temperature;
    body[QStringLiteral("max_tokens")] = max_tokens;

    if (((!reasoning_effort.isEmpty() && reasoning_effort != QStringLiteral("off")) == true))
    {
        body[QStringLiteral("reasoning_effort")] = reasoning_effort;
    }

    const bool streaming = (stream_callback != nullptr);
    if (streaming == true)
    {
        body[QStringLiteral("stream")] = true;
        body[QStringLiteral("stream_options")] =
            QJsonObject{{QStringLiteral("include_usage"), true}};
    }

    // Build URL
    QString urlStr = this->base_url;
    if (((urlStr.endsWith(QLatin1Char('/'))) == false))
    {
        urlStr += QLatin1Char('/');
    }
    urlStr += QStringLiteral("v1/chat/completions");

    QCAI_DEBUG("OpenAI", QStringLiteral("POST %1 | model=%2 temp=%3 maxTok=%4 stream=%5 msgs=%6")
                             .arg(urlStr, model)
                             .arg(temperature)
                             .arg(max_tokens)
                             .arg(streaming ? QStringLiteral("yes") : QStringLiteral("no"))
                             .arg(messages.size()));

    QUrl url(urlStr);
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (((!this->api_key.isEmpty()) == true))
    {
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(this->api_key).toUtf8());
    }

    for (auto it = this->extra_headers.begin(); ((it != this->extra_headers.end()) == true); ++it)
    {
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    if (streaming == false)
    {
        req.setTransferTimeout(15000);
        req.setRawHeader("Connection", "close");
    }

    this->sse_buffer.clear();
    this->stream_accum.clear();
    this->stream_usage = {};

    this->current_reply = this->nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (streaming == true)
    {
        const auto activeTools = std::make_shared<QSet<QString>>();

        // Stream mode: process SSE chunks as they arrive
        connect(
            this->current_reply, &QNetworkReply::readyRead, this,
            [this, stream_callback, progress_callback, activeTools]() {
                this->sse_buffer.append(this->current_reply->readAll());

                // Process complete SSE lines
                while (true == true)
                {
                    qsizetype idx = this->sse_buffer.indexOf('\n');
                    if (idx < 0)
                    {
                        break;
                    }

                    QByteArray line = this->sse_buffer.left(idx).trimmed();
                    this->sse_buffer.remove(0, idx + 1);

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
                    const provider_usage_t usage = provider_usage_from_response_object(obj);
                    if (usage.has_any() == true)
                    {
                        this->stream_usage = usage;
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
                        if (progress_callback != nullptr)
                        {
                            progress_callback({provider_raw_event_kind_t::REASONING_DELTA,
                                               this->id(),
                                               QStringLiteral("response.reasoning.delta"),
                                               {},
                                               reasoningDelta});
                        }
                        stream_callback(reasoningDelta);
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
                        if (!toolName.isEmpty() && progress_callback != nullptr)
                        {
                            progress_callback({provider_raw_event_kind_t::TOOL_STARTED,
                                               this->id(),
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
                        this->stream_accum += delta;
                        if (progress_callback != nullptr)
                        {
                            progress_callback({provider_raw_event_kind_t::MESSAGE_DELTA,
                                               this->id(),
                                               QStringLiteral("assistant.message_delta"),
                                               {},
                                               delta});
                        }
                        stream_callback(delta);
                    }

                    const QString finishReason =
                        choices[0].toObject().value(QStringLiteral("finish_reason")).toString();
                    if (finishReason == QStringLiteral("tool_calls") &&
                        progress_callback != nullptr)
                    {
                        for (const QString &toolName : std::as_const(*activeTools))
                        {
                            progress_callback({provider_raw_event_kind_t::TOOL_COMPLETED,
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
            this->current_reply, &QNetworkReply::finished, this,
            [this, callback, stream_callback, progress_callback, activeTools]() {
                QNetworkReply *reply = this->current_reply;
                this->current_reply = nullptr;

                if (((reply == nullptr) == true))
                {
                    callback({}, QStringLiteral("Reply was null"), {});
                    return;
                }

                if (((reply->error() != QNetworkReply::NoError &&
                      reply->error() != QNetworkReply::OperationCanceledError) == true))
                {
                    const QString errBody = QString::fromUtf8(reply->readAll());
                    if (progress_callback != nullptr)
                    {
                        progress_callback({provider_raw_event_kind_t::ERROR_EVENT,
                                           this->id(),
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

                if (progress_callback != nullptr)
                {
                    for (const QString &toolName : std::as_const(*activeTools))
                    {
                        progress_callback({provider_raw_event_kind_t::TOOL_COMPLETED,
                                           id(),
                                           QStringLiteral("response.tool_call.completed"),
                                           toolName,
                                           {}});
                    }
                    progress_callback({provider_raw_event_kind_t::RESPONSE_COMPLETED,
                                       this->id(),
                                       QStringLiteral("response.completed"),
                                       {},
                                       {}});
                }
                const provider_usage_t usage = this->stream_usage;
                reply->deleteLater();
                QCAI_DEBUG("OpenAI", QStringLiteral("Stream complete, accumulated %1 chars")
                                         .arg(this->stream_accum.length()));
                stream_callback({});  // signal stream end
                callback(this->stream_accum, {}, usage);
                this->stream_accum.clear();
                this->sse_buffer.clear();
                this->stream_usage = {};
            });
    }
    else
    {
        // Non-streaming mode (original)
        connect(
            this->current_reply, &QNetworkReply::finished, this,
            [this, callback, progress_callback]() {
                QNetworkReply *reply = this->current_reply;
                this->current_reply = nullptr;

                if (!reply)
                {
                    callback({}, QStringLiteral("Reply was null"), {});
                    return;
                }

                if (reply->error() != QNetworkReply::NoError)
                {
                    const QString errBody = QString::fromUtf8(reply->readAll());
                    if (progress_callback != nullptr)
                    {
                        progress_callback({provider_raw_event_kind_t::ERROR_EVENT,
                                           this->id(),
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

                const provider_usage_t usage = provider_usage_from_response_object(doc.object());
                const QString content =
                    json::get_string(doc.object(), QStringLiteral("choices/0/message/content"));
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
                if (progress_callback != nullptr)
                {
                    progress_callback({provider_raw_event_kind_t::RESPONSE_COMPLETED,
                                       this->id(),
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
void open_ai_compatible_provider_t::cancel()
{
    if (this->current_reply != nullptr)
    {
        this->current_reply->abort();
        this->current_reply->deleteLater();
        this->current_reply = nullptr;
    }
}

}  // namespace qcai2
