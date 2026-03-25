/*! @file
    @brief Implements local HTTP completion requests.
*/

#include "local_http_provider.h"
#include "../util/json.h"
#include "../util/logger.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkRequest>
#include <QUrl>

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

QJsonObject open_ai_message_to_json(const chat_message_t &message, QString *error)
{
    QJsonObject json{{QStringLiteral("role"), message.role}};
    if (message.attachments.isEmpty() == true)
    {
        json.insert(QStringLiteral("content"), message.content);
        return json;
    }

    QJsonArray content_parts;
    if (message.content.isEmpty() == false)
    {
        content_parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                         {QStringLiteral("text"), message.content}});
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

        const QString mime_type = attachment.mime_type.trimmed().isEmpty() == false
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

        const QString data_url =
            QStringLiteral("data:%1;base64,%2")
                .arg(mime_type, QString::fromLatin1(file.readAll().toBase64()));
        if (is_image_mime_type(mime_type) == true)
        {
            content_parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), data_url}}}});
            continue;
        }
        if (is_pdf_mime_type(mime_type) == true)
        {
            content_parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("file")},
                {QStringLiteral("file"),
                 QJsonObject{{QStringLiteral("filename"), attachment_display_name(attachment)},
                             {QStringLiteral("file_data"), data_url}}}});
            continue;
        }

        if (error != nullptr)
        {
            *error =
                QStringLiteral("The current provider '%1' does not support attachment '%2' (%3).")
                    .arg(QStringLiteral("Local HTTP"), attachment_display_name(attachment),
                         mime_type);
        }
        return {};
    }

    json.insert(QStringLiteral("content"), content_parts);
    return json;
}

}  // namespace

QString local_http_provider_t::attachment_support_error(const file_attachment_t &attachment) const
{
    if (this->simple_mode == true)
    {
        return QStringLiteral(
            "The configured local provider simple mode does not support file attachments.");
    }

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

local_http_provider_t::local_http_provider_t(QObject *parent) : QObject(parent)
{
}

void local_http_provider_t::complete(const QList<chat_message_t> &messages, const QString &model,
                                     double temperature, int max_tokens,
                                     const QString &reasoning_effort,
                                     completion_callback_t callback,
                                     stream_callback_t /*stream_callback*/,
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

    QJsonObject body;

    if (this->simple_mode == true)
    {
        // Simple mode: concatenate messages into a single prompt string
        QString prompt;
        for (const auto &m : messages)
        {
            if (m.attachments.isEmpty() == false)
            {
                callback({},
                         QStringLiteral("The configured local provider simple mode does not "
                                        "support file attachments."),
                         {});
                return;
            }
            prompt += QStringLiteral("[%1]: %2\n").arg(m.role, m.content);
        }
        body["prompt"] = prompt;
        if (model.isEmpty() == false)
        {
            body["model"] = model;
        }
        body["temperature"] = temperature;
        body["max_tokens"] = max_tokens;
    }
    else
    {
        // OpenAI-compatible format
        QJsonArray msgArr;
        for (const auto &m : messages)
        {
            QString message_error;
            const QJsonObject message_json = open_ai_message_to_json(m, &message_error);
            if (message_error.isEmpty() == false)
            {
                callback({}, message_error, {});
                return;
            }
            msgArr.append(message_json);
        }
        body["model"] = model;
        body["messages"] = msgArr;
        body["temperature"] = temperature;
        body["max_tokens"] = max_tokens;
        if (((!reasoning_effort.isEmpty() && reasoning_effort != QStringLiteral("off")) == true))
        {
            body["reasoning_effort"] = reasoning_effort;
        }
    }

    QString urlStr = this->base_url;
    if (urlStr.endsWith('/') == true)
    {
        urlStr.chop(1);
    }
    urlStr += this->endpoint_path;

    QCAI_DEBUG(
        "LocalHTTP",
        QStringLiteral("POST %1 | model=%2 simple=%3 msgs=%4")
            .arg(urlStr, model, this->simple_mode ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(messages.size()));

    QUrl url(urlStr);
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    if (((!this->api_key.isEmpty()) == true))
    {
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(this->api_key).toUtf8());
    }

    for (auto it = this->custom_headers.begin(); ((it != this->custom_headers.end()) == true);
         ++it)
    {
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    req.setTransferTimeout(15000);
    req.setRawHeader("Connection", "close");

    this->current_reply = this->nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

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
                const auto errBody = QString::fromUtf8(reply->readAll());
                if (progress_callback != nullptr)
                {
                    progress_callback({provider_raw_event_kind_t::ERROR_EVENT,
                                       this->id(),
                                       QStringLiteral("response.error"),
                                       {},
                                       reply->errorString()});
                }
                QCAI_ERROR("LocalHTTP", QStringLiteral("HTTP error: %1 — %2")
                                            .arg(reply->errorString(), errBody.left(300)));
                callback({},
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
                callback({}, QStringLiteral("JSON parse error: %1").arg(err.errorString()), {});
                return;
            }

            const QJsonObject root = doc.object();
            const provider_usage_t usage = provider_usage_from_response_object(root);

            // Try OpenAI format first
            QString content = json::get_string(root, QStringLiteral("choices/0/message/content"));
            if (content.isEmpty())
            {
                // Try simple format: {"response": "..."}
                content = root.value("response").toString();
            }
            if (content.isEmpty())
            {
                // Try {"text": "..."}
                content = root.value("text").toString();
            }
            if (content.isEmpty())
            {
                callback({},
                         QStringLiteral("Could not extract content from: %1")
                             .arg(QString::fromUtf8(data)),
                         {});
                return;
            }

            if (progress_callback != nullptr)
            {
                progress_callback({provider_raw_event_kind_t::RESPONSE_COMPLETED,
                                   this->id(),
                                   QStringLiteral("response.completed"),
                                   {},
                                   {}});
            }
            callback(content, {}, usage);
            QCAI_DEBUG("LocalHTTP",
                       QStringLiteral("Response received, %1 chars").arg(content.length()));
        });
}

void local_http_provider_t::cancel()
{
    if (this->current_reply != nullptr)
    {
        this->current_reply->abort();
        this->current_reply->deleteLater();
        this->current_reply = nullptr;
    }
}

}  // namespace qcai2
