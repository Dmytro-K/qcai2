#include "anthropic_provider.h"

#include "../util/json.h"
#include "../util/logger.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

namespace qcai2
{

namespace
{

QString anthropicErrorMessage(const QByteArray &data, const QString &fallback)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (((parseError.error == QJsonParseError::NoError) == true) && (doc.isObject() == true))
    {
        const QString message =
            json::get_string(doc.object(), QStringLiteral("error/message"), fallback);
        if (message.isEmpty() == false)
        {
            return message;
        }
    }

    return fallback;
}

QJsonArray anthropicMessagesFromChatMessages(const QList<chat_message_t> &messages,
                                             QString *systemText)
{
    QJsonArray anthropicMessages;
    QStringList systemParts;

    for (const chat_message_t &message : messages)
    {
        if (message.content.trimmed().isEmpty() == true)
        {
            continue;
        }

        if (message.role == QStringLiteral("system"))
        {
            systemParts.append(message.content);
            continue;
        }

        const QString role = (message.role == QStringLiteral("assistant"))
                                 ? QStringLiteral("assistant")
                                 : QStringLiteral("user");
        anthropicMessages.append(
            QJsonObject{{QStringLiteral("role"), role},
                        {QStringLiteral("content"),
                         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                {QStringLiteral("text"), message.content}}}}});
    }

    if (systemText != nullptr)
    {
        *systemText = systemParts.join(QStringLiteral("\n\n")).trimmed();
    }

    return anthropicMessages;
}

QString anthropicTextFromBlocks(const QJsonArray &contentBlocks)
{
    QStringList textParts;
    for (const QJsonValue &blockValue : contentBlocks)
    {
        if (blockValue.isObject() == false)
        {
            continue;
        }

        const QJsonObject block = blockValue.toObject();
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
        {
            const QString text = block.value(QStringLiteral("text")).toString();
            if (text.isEmpty() == false)
            {
                textParts.append(text);
            }
        }
    }

    return textParts.join(QString());
}

QByteArray ssePayloadForLine(const QByteArray &line)
{
    if (line.startsWith("data: ") == true)
    {
        return line.mid(6);
    }
    if (line.startsWith("data:") == true)
    {
        return line.mid(5).trimmed();
    }
    return {};
}

}  // namespace

anthropic_provider_t::anthropic_provider_t(QObject *parent) : QObject(parent)
{
}

void anthropic_provider_t::complete(const QList<chat_message_t> &messages, const QString &model,
                                    double temperature, int max_tokens,
                                    const QString &reasoning_effort,
                                    completion_callback_t callback,
                                    stream_callback_t stream_callback,
                                    progress_callback_t progress_callback)
{
    Q_UNUSED(reasoning_effort);

    if (progress_callback != nullptr)
    {
        progress_callback(provider_raw_event_t{provider_raw_event_kind_t::REQUEST_STARTED,
                                               this->id(),
                                               QStringLiteral("request.started"),
                                               {},
                                               {}});
    }

    QString systemText;
    const QJsonArray anthropicMessages = anthropicMessagesFromChatMessages(messages, &systemText);

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), anthropicMessages);
    body.insert(QStringLiteral("max_tokens"), max_tokens);
    body.insert(QStringLiteral("temperature"), temperature);
    if (systemText.isEmpty() == false)
    {
        body.insert(QStringLiteral("system"), systemText);
    }

    const bool streaming = (stream_callback != nullptr);
    if (streaming == true)
    {
        body.insert(QStringLiteral("stream"), true);
    }

    QString urlString = this->base_url.trimmed();
    if (urlString.endsWith(QLatin1Char('/')) == true)
    {
        urlString.chop(1);
    }
    urlString += QStringLiteral("/v1/messages");

    QCAI_DEBUG("Anthropic",
               QStringLiteral("POST %1 | model=%2 temp=%3 maxTok=%4 stream=%5 msgs=%6")
                   .arg(urlString, model)
                   .arg(temperature)
                   .arg(max_tokens)
                   .arg(streaming ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(messages.size()));

    QNetworkRequest request{QUrl(urlString)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("anthropic-version", "2023-06-01");
    if (((!this->api_key.isEmpty()) == true))
    {
        request.setRawHeader("x-api-key", this->api_key.toUtf8());
    }
    if (streaming == false)
    {
        request.setTransferTimeout(15000);
        request.setRawHeader("Connection", "close");
    }

    this->sse_buffer.clear();
    this->stream_accum.clear();
    this->stream_usage = {};

    this->current_reply =
        this->nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (streaming == true)
    {
        const auto activeToolBlocks = std::make_shared<QHash<int, QString>>();
        const auto currentEventName = std::make_shared<QString>();
        const auto currentEventData = std::make_shared<QByteArray>();
        const auto streamError = std::make_shared<QString>();

        const auto processEvent = [this, stream_callback, progress_callback, activeToolBlocks,
                                   streamError](const QString &eventName,
                                                const QByteArray &eventData) {
            if (eventData.isEmpty() == true)
            {
                return;
            }

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(eventData, &parseError);
            if (((parseError.error != QJsonParseError::NoError || !doc.isObject()) == true))
            {
                return;
            }

            const QJsonObject payload = doc.object();
            const QString effectiveEvent = eventName.isEmpty() == false
                                               ? eventName
                                               : payload.value(QStringLiteral("type")).toString();

            if (effectiveEvent == QStringLiteral("ping"))
            {
                return;
            }

            if (effectiveEvent == QStringLiteral("error"))
            {
                *streamError =
                    anthropicErrorMessage(eventData, payload.value(QStringLiteral("error"))
                                                         .toObject()
                                                         .value(QStringLiteral("message"))
                                                         .toString());
                if (progress_callback != nullptr)
                {
                    progress_callback(provider_raw_event_t{provider_raw_event_kind_t::ERROR_EVENT,
                                                           this->id(),
                                                           QStringLiteral("error"),
                                                           {},
                                                           *streamError});
                }
                return;
            }

            const provider_usage_t usage = provider_usage_from_response_object(payload);
            if (usage.has_any() == true)
            {
                this->stream_usage = usage;
            }

            if (effectiveEvent == QStringLiteral("content_block_start"))
            {
                const QJsonObject block =
                    payload.value(QStringLiteral("content_block")).toObject();
                const QString blockType = block.value(QStringLiteral("type")).toString();
                const int index = payload.value(QStringLiteral("index")).toInt(-1);
                if (blockType == QStringLiteral("tool_use"))
                {
                    const QString toolName =
                        block.value(QStringLiteral("name")).toString().trimmed();
                    if (((index >= 0) == true) && (toolName.isEmpty() == false))
                    {
                        activeToolBlocks->insert(index, toolName);
                    }
                    if (toolName.isEmpty() == false && progress_callback != nullptr)
                    {
                        progress_callback(
                            provider_raw_event_t{provider_raw_event_kind_t::TOOL_STARTED,
                                                 this->id(),
                                                 QStringLiteral("content_block_start"),
                                                 toolName,
                                                 {}});
                    }
                }
                else if (blockType == QStringLiteral("thinking"))
                {
                    if (progress_callback != nullptr)
                    {
                        progress_callback(
                            provider_raw_event_t{provider_raw_event_kind_t::REASONING_DELTA,
                                                 this->id(),
                                                 QStringLiteral("content_block_start"),
                                                 {},
                                                 {}});
                    }
                }
                return;
            }

            if (effectiveEvent == QStringLiteral("content_block_delta"))
            {
                const QJsonObject delta = payload.value(QStringLiteral("delta")).toObject();
                const QString deltaType = delta.value(QStringLiteral("type")).toString();
                if (deltaType == QStringLiteral("text_delta"))
                {
                    const QString text = delta.value(QStringLiteral("text")).toString();
                    if (text.isEmpty() == false)
                    {
                        this->stream_accum += text;
                        if (progress_callback != nullptr)
                        {
                            progress_callback(
                                provider_raw_event_t{provider_raw_event_kind_t::MESSAGE_DELTA,
                                                     this->id(),
                                                     QStringLiteral("content_block_delta"),
                                                     {},
                                                     text});
                        }
                        stream_callback(text);
                    }
                }
                else if (deltaType == QStringLiteral("thinking_delta"))
                {
                    if (progress_callback != nullptr)
                    {
                        progress_callback(
                            provider_raw_event_t{provider_raw_event_kind_t::REASONING_DELTA,
                                                 this->id(),
                                                 QStringLiteral("content_block_delta"),
                                                 {},
                                                 {}});
                    }
                }
                return;
            }

            if (effectiveEvent == QStringLiteral("content_block_stop"))
            {
                const int index = payload.value(QStringLiteral("index")).toInt(-1);
                if (((index >= 0) == true) && (activeToolBlocks->contains(index) == true))
                {
                    const QString toolName = activeToolBlocks->take(index);
                    if (progress_callback != nullptr)
                    {
                        progress_callback(
                            provider_raw_event_t{provider_raw_event_kind_t::TOOL_COMPLETED,
                                                 this->id(),
                                                 QStringLiteral("content_block_stop"),
                                                 toolName,
                                                 {}});
                    }
                }
                return;
            }
        };

        connect(this->current_reply, &QNetworkReply::readyRead, this,
                [this, currentEventName, currentEventData, processEvent]() {
                    this->sse_buffer.append(this->current_reply->readAll());

                    while (true == true)
                    {
                        const qsizetype lineEnd = this->sse_buffer.indexOf('\n');
                        if (lineEnd < 0)
                        {
                            break;
                        }

                        QByteArray line = this->sse_buffer.left(lineEnd);
                        this->sse_buffer.remove(0, lineEnd + 1);
                        if (line.endsWith('\r') == true)
                        {
                            line.chop(1);
                        }

                        if (line.isEmpty() == true)
                        {
                            processEvent(*currentEventName, *currentEventData);
                            currentEventName->clear();
                            currentEventData->clear();
                            continue;
                        }

                        if (line.startsWith("event: ") == true)
                        {
                            *currentEventName = QString::fromUtf8(line.mid(7).trimmed());
                            continue;
                        }
                        if (line.startsWith("event:") == true)
                        {
                            *currentEventName = QString::fromUtf8(line.mid(6).trimmed());
                            continue;
                        }
                        if (line.startsWith("data:") == true)
                        {
                            const QByteArray payload = ssePayloadForLine(line);
                            if (payload.isEmpty() == false)
                            {
                                if (currentEventData->isEmpty() == false)
                                {
                                    currentEventData->append('\n');
                                }
                                currentEventData->append(payload);
                            }
                        }
                    }
                });

        connect(this->current_reply, &QNetworkReply::finished, this,
                [this, callback, stream_callback, progress_callback, currentEventName,
                 currentEventData, processEvent, streamError]() {
                    QNetworkReply *reply = this->current_reply;
                    this->current_reply = nullptr;

                    processEvent(*currentEventName, *currentEventData);
                    currentEventName->clear();
                    currentEventData->clear();

                    if (((reply == nullptr) == true))
                    {
                        callback({}, QStringLiteral("Reply was null"), {});
                        return;
                    }

                    if (((reply->error() != QNetworkReply::NoError &&
                          reply->error() != QNetworkReply::OperationCanceledError) == true))
                    {
                        const QByteArray data = reply->readAll();
                        const QString message = anthropicErrorMessage(data, reply->errorString());
                        if (progress_callback != nullptr)
                        {
                            progress_callback(
                                provider_raw_event_t{provider_raw_event_kind_t::ERROR_EVENT,
                                                     this->id(),
                                                     QStringLiteral("response.error"),
                                                     {},
                                                     message});
                        }
                        callback({}, message, {});
                        reply->deleteLater();
                        return;
                    }

                    if (streamError->isEmpty() == false)
                    {
                        callback({}, *streamError, {});
                        reply->deleteLater();
                        return;
                    }

                    if (progress_callback != nullptr)
                    {
                        progress_callback(
                            provider_raw_event_t{provider_raw_event_kind_t::RESPONSE_COMPLETED,
                                                 this->id(),
                                                 QStringLiteral("message_stop"),
                                                 {},
                                                 {}});
                    }
                    const provider_usage_t usage = this->stream_usage;
                    reply->deleteLater();
                    stream_callback({});
                    callback(this->stream_accum, {}, usage);
                    this->stream_accum.clear();
                    this->sse_buffer.clear();
                    this->stream_usage = {};
                });
    }
    else
    {
        connect(
            this->current_reply, &QNetworkReply::finished, this,
            [this, callback, progress_callback]() {
                QNetworkReply *reply = this->current_reply;
                this->current_reply = nullptr;

                if (((reply == nullptr) == true))
                {
                    callback({}, QStringLiteral("Reply was null"), {});
                    return;
                }

                if (((reply->error() != QNetworkReply::NoError) == true))
                {
                    const QByteArray data = reply->readAll();
                    const QString message = anthropicErrorMessage(data, reply->errorString());
                    if (progress_callback != nullptr)
                    {
                        progress_callback(
                            provider_raw_event_t{provider_raw_event_kind_t::ERROR_EVENT,
                                                 this->id(),
                                                 QStringLiteral("response.error"),
                                                 {},
                                                 message});
                    }
                    callback({}, message, {});
                    reply->deleteLater();
                    return;
                }

                const QByteArray data = reply->readAll();
                reply->deleteLater();

                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
                if (((parseError.error != QJsonParseError::NoError || !doc.isObject()) == true))
                {
                    callback({},
                             QStringLiteral("JSON parse error: %1").arg(parseError.errorString()),
                             {});
                    return;
                }

                const QJsonObject root = doc.object();
                const provider_usage_t usage = provider_usage_from_response_object(root);
                const QString content =
                    anthropicTextFromBlocks(root.value(QStringLiteral("content")).toArray());
                if (content.isEmpty() == true)
                {
                    callback({}, QStringLiteral("No text content in response."), {});
                    return;
                }

                if (progress_callback != nullptr)
                {
                    progress_callback(
                        provider_raw_event_t{provider_raw_event_kind_t::RESPONSE_COMPLETED,
                                             this->id(),
                                             QStringLiteral("response.completed"),
                                             {},
                                             {}});
                }
                callback(content, {}, usage);
            });
    }
}

void anthropic_provider_t::cancel()
{
    if (this->current_reply != nullptr)
    {
        this->current_reply->abort();
        this->current_reply->deleteLater();
        this->current_reply = nullptr;
    }
}

}  // namespace qcai2
