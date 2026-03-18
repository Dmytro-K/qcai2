/*! @file
    @brief Implements local HTTP completion requests.
*/

#include "local_http_provider.h"
#include "../util/json.h"
#include "../util/logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

namespace qcai2
{

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
            msgArr.append(m.to_json());
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
