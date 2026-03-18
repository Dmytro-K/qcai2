#include "AnthropicProvider.h"

#include "../util/Json.h"
#include "../util/Logger.h"

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
            Json::getString(doc.object(), QStringLiteral("error/message"), fallback);
        if (message.isEmpty() == false)
        {
            return message;
        }
    }

    return fallback;
}

QJsonArray anthropicMessagesFromChatMessages(const QList<ChatMessage> &messages,
                                             QString *systemText)
{
    QJsonArray anthropicMessages;
    QStringList systemParts;

    for (const ChatMessage &message : messages)
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

AnthropicProvider::AnthropicProvider(QObject *parent) : QObject(parent)
{
}

void AnthropicProvider::complete(const QList<ChatMessage> &messages, const QString &model,
                                 double temperature, int maxTokens, const QString &reasoningEffort,
                                 CompletionCallback callback, StreamCallback streamCallback,
                                 ProgressCallback progressCallback)
{
    Q_UNUSED(reasoningEffort);

    if (progressCallback != nullptr)
    {
        progressCallback(ProviderRawEvent{ProviderRawEventKind::RequestStarted,
                                          id(),
                                          QStringLiteral("request.started"),
                                          {},
                                          {}});
    }

    QString systemText;
    const QJsonArray anthropicMessages = anthropicMessagesFromChatMessages(messages, &systemText);

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), anthropicMessages);
    body.insert(QStringLiteral("max_tokens"), maxTokens);
    body.insert(QStringLiteral("temperature"), temperature);
    if (systemText.isEmpty() == false)
    {
        body.insert(QStringLiteral("system"), systemText);
    }

    const bool streaming = (streamCallback != nullptr);
    if (streaming == true)
    {
        body.insert(QStringLiteral("stream"), true);
    }

    QString urlString = m_baseUrl.trimmed();
    if (urlString.endsWith(QLatin1Char('/')) == true)
    {
        urlString.chop(1);
    }
    urlString += QStringLiteral("/v1/messages");

    QCAI_DEBUG("Anthropic",
               QStringLiteral("POST %1 | model=%2 temp=%3 maxTok=%4 stream=%5 msgs=%6")
                   .arg(urlString, model)
                   .arg(temperature)
                   .arg(maxTokens)
                   .arg(streaming ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(messages.size()));

    QNetworkRequest request{QUrl(urlString)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("anthropic-version", "2023-06-01");
    if (((!m_apiKey.isEmpty()) == true))
    {
        request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    }
    if (streaming == false)
    {
        request.setTransferTimeout(15000);
        request.setRawHeader("Connection", "close");
    }

    m_sseBuffer.clear();
    m_streamAccum.clear();
    m_streamUsage = {};

    m_currentReply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (streaming == true)
    {
        const auto activeToolBlocks = std::make_shared<QHash<int, QString>>();
        const auto currentEventName = std::make_shared<QString>();
        const auto currentEventData = std::make_shared<QByteArray>();
        const auto streamError = std::make_shared<QString>();

        const auto processEvent = [this, streamCallback, progressCallback, activeToolBlocks,
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
                if (progressCallback != nullptr)
                {
                    progressCallback(ProviderRawEvent{ProviderRawEventKind::Error,
                                                      id(),
                                                      QStringLiteral("error"),
                                                      {},
                                                      *streamError});
                }
                return;
            }

            const ProviderUsage usage = providerUsageFromResponseObject(payload);
            if (usage.hasAny() == true)
            {
                m_streamUsage = usage;
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
                    if (toolName.isEmpty() == false && progressCallback != nullptr)
                    {
                        progressCallback(ProviderRawEvent{ProviderRawEventKind::ToolStarted,
                                                          id(),
                                                          QStringLiteral("content_block_start"),
                                                          toolName,
                                                          {}});
                    }
                }
                else if (blockType == QStringLiteral("thinking"))
                {
                    if (progressCallback != nullptr)
                    {
                        progressCallback(ProviderRawEvent{ProviderRawEventKind::ReasoningDelta,
                                                          id(),
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
                        m_streamAccum += text;
                        if (progressCallback != nullptr)
                        {
                            progressCallback(
                                ProviderRawEvent{ProviderRawEventKind::MessageDelta,
                                                 id(),
                                                 QStringLiteral("content_block_delta"),
                                                 {},
                                                 text});
                        }
                        streamCallback(text);
                    }
                }
                else if (deltaType == QStringLiteral("thinking_delta"))
                {
                    if (progressCallback != nullptr)
                    {
                        progressCallback(ProviderRawEvent{ProviderRawEventKind::ReasoningDelta,
                                                          id(),
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
                    if (progressCallback != nullptr)
                    {
                        progressCallback(ProviderRawEvent{ProviderRawEventKind::ToolCompleted,
                                                          id(),
                                                          QStringLiteral("content_block_stop"),
                                                          toolName,
                                                          {}});
                    }
                }
                return;
            }
        };

        connect(m_currentReply, &QNetworkReply::readyRead, this,
                [this, currentEventName, currentEventData, processEvent]() {
                    m_sseBuffer.append(m_currentReply->readAll());

                    while (true == true)
                    {
                        const qsizetype lineEnd = m_sseBuffer.indexOf('\n');
                        if (lineEnd < 0)
                        {
                            break;
                        }

                        QByteArray line = m_sseBuffer.left(lineEnd);
                        m_sseBuffer.remove(0, lineEnd + 1);
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

        connect(m_currentReply, &QNetworkReply::finished, this,
                [this, callback, streamCallback, progressCallback, currentEventName,
                 currentEventData, processEvent, streamError]() {
                    QNetworkReply *reply = m_currentReply;
                    m_currentReply = nullptr;

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
                        if (progressCallback != nullptr)
                        {
                            progressCallback(ProviderRawEvent{ProviderRawEventKind::Error,
                                                              id(),
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

                    if (progressCallback != nullptr)
                    {
                        progressCallback(ProviderRawEvent{ProviderRawEventKind::ResponseCompleted,
                                                          id(),
                                                          QStringLiteral("message_stop"),
                                                          {},
                                                          {}});
                    }
                    const ProviderUsage usage = m_streamUsage;
                    reply->deleteLater();
                    streamCallback({});
                    callback(m_streamAccum, {}, usage);
                    m_streamAccum.clear();
                    m_sseBuffer.clear();
                    m_streamUsage = {};
                });
    }
    else
    {
        connect(
            m_currentReply, &QNetworkReply::finished, this, [this, callback, progressCallback]() {
                QNetworkReply *reply = m_currentReply;
                m_currentReply = nullptr;

                if (((reply == nullptr) == true))
                {
                    callback({}, QStringLiteral("Reply was null"), {});
                    return;
                }

                if (((reply->error() != QNetworkReply::NoError) == true))
                {
                    const QByteArray data = reply->readAll();
                    const QString message = anthropicErrorMessage(data, reply->errorString());
                    if (progressCallback != nullptr)
                    {
                        progressCallback(ProviderRawEvent{ProviderRawEventKind::Error,
                                                          id(),
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
                const ProviderUsage usage = providerUsageFromResponseObject(root);
                const QString content =
                    anthropicTextFromBlocks(root.value(QStringLiteral("content")).toArray());
                if (content.isEmpty() == true)
                {
                    callback({}, QStringLiteral("No text content in response."), {});
                    return;
                }

                if (progressCallback != nullptr)
                {
                    progressCallback(ProviderRawEvent{ProviderRawEventKind::ResponseCompleted,
                                                      id(),
                                                      QStringLiteral("response.completed"),
                                                      {},
                                                      {}});
                }
                callback(content, {}, usage);
            });
    }
}

void AnthropicProvider::cancel()
{
    if (m_currentReply != nullptr)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

}  // namespace qcai2
