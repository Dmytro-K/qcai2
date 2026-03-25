#include "anthropic_provider.h"

#include "../util/json.h"
#include "../util/logger.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

namespace qcai2
{

namespace
{

QString attachment_display_name(const file_attachment_t &attachment)
{
    if (attachment.file_name.trimmed().isEmpty() == false)
    {
        return attachment.file_name.trimmed();
    }
    return QFileInfo(attachment.storage_path).fileName();
}

bool is_image_mime_type(const QString &mime_type)
{
    return mime_type.startsWith(QStringLiteral("image/"));
}

bool is_pdf_mime_type(const QString &mime_type)
{
    return mime_type == QStringLiteral("application/pdf");
}

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

QJsonObject anthropicTextBlock(const QString &text, bool cache_control = false)
{
    QJsonObject block{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), text},
    };
    if (cache_control == true)
    {
        block.insert(QStringLiteral("cache_control"),
                     QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}});
    }
    return block;
}

QJsonArray anthropicMessagesFromChatMessages(const QList<chat_message_t> &messages,
                                             QJsonArray *system_blocks, QString *error)
{
    QJsonArray anthropicMessages;
    QJsonArray local_system_blocks;
    int cacheable_system_block_count = 0;

    for (const chat_message_t &message : messages)
    {
        if (message.content.trimmed().isEmpty() == true && message.attachments.isEmpty() == true)
        {
            continue;
        }

        if (message.role == QStringLiteral("system"))
        {
            if (message.attachments.isEmpty() == false)
            {
                continue;
            }
            const bool cache_control = cacheable_system_block_count < 4;
            local_system_blocks.append(anthropicTextBlock(message.content, cache_control));
            ++cacheable_system_block_count;
            continue;
        }

        QJsonArray content_blocks;
        if (message.content.isEmpty() == false)
        {
            content_blocks.append(anthropicTextBlock(message.content));
        }
        const QMimeDatabase mime_db;
        for (const file_attachment_t &attachment : message.attachments)
        {
            QFile file(attachment.storage_path);
            if (file.open(QIODevice::ReadOnly) == false)
            {
                if (error != nullptr)
                {
                    *error = QStringLiteral("Failed to read image attachment: %1")
                                 .arg(attachment.storage_path);
                }
                return {};
            }
            const QString mime_type =
                attachment.mime_type.trimmed().isEmpty() == false
                    ? attachment.mime_type.trimmed()
                    : mime_db.mimeTypeForFile(attachment.storage_path).name();
            if (mime_type.isEmpty() == true)
            {
                if (error != nullptr)
                {
                    *error = QStringLiteral("Failed to detect image MIME type: %1")
                                 .arg(attachment.storage_path);
                }
                return {};
            }
            const QString base64_data = QString::fromLatin1(file.readAll().toBase64());
            if (is_image_mime_type(mime_type) == true)
            {
                content_blocks.append(
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                {QStringLiteral("source"),
                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                             {QStringLiteral("media_type"), mime_type},
                                             {QStringLiteral("data"), base64_data}}}});
                continue;
            }
            if (is_pdf_mime_type(mime_type) == true)
            {
                content_blocks.append(
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("document")},
                                {QStringLiteral("source"),
                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                             {QStringLiteral("media_type"), mime_type},
                                             {QStringLiteral("data"), base64_data}}}});
                continue;
            }
            if (error != nullptr)
            {
                *error = QStringLiteral(
                             "The current provider '%1' does not support attachment '%2' (%3).")
                             .arg(QStringLiteral("Anthropic"), attachment_display_name(attachment),
                                  mime_type);
            }
            return {};
        }

        const QString role = (message.role == QStringLiteral("assistant"))
                                 ? QStringLiteral("assistant")
                                 : QStringLiteral("user");
        anthropicMessages.append(QJsonObject{{QStringLiteral("role"), role},
                                             {QStringLiteral("content"), content_blocks}});
    }

    if (system_blocks != nullptr)
    {
        *system_blocks = local_system_blocks;
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

QString anthropic_provider_t::attachment_support_error(const file_attachment_t &attachment) const
{
    const QString mime_type = attachment.mime_type.trimmed();
    if (mime_type.startsWith(QStringLiteral("image/")) == true ||
        mime_type == QStringLiteral("application/pdf"))
    {
        return {};
    }
    return QStringLiteral("The current provider '%1' supports only image and PDF attachments, but "
                          "'%2' has MIME type '%3'.")
        .arg(this->display_name(), attachment_display_name(attachment),
             mime_type.isEmpty() == false ? mime_type : QStringLiteral("unknown"));
}

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

    QJsonArray system_blocks;
    QString message_error;
    const QJsonArray anthropicMessages =
        anthropicMessagesFromChatMessages(messages, &system_blocks, &message_error);
    if (message_error.isEmpty() == false)
    {
        callback({}, message_error, {});
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), anthropicMessages);
    body.insert(QStringLiteral("max_tokens"), max_tokens);
    body.insert(QStringLiteral("temperature"), temperature);
    if (system_blocks.isEmpty() == false)
    {
        body.insert(QStringLiteral("system"), system_blocks);
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
