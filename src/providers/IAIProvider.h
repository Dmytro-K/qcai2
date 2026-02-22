#pragma once

#include "../models/AgentMessages.h"

#include <QObject>
#include <QString>
#include <QList>
#include <functional>

namespace Qcai2 {

// Abstract interface for AI model providers.
class IAIProvider
{
public:
    virtual ~IAIProvider() = default;

    // Unique provider identifier (e.g. "openai", "local", "ollama")
    virtual QString id() const = 0;

    // Human-readable display name
    virtual QString displayName() const = 0;

    // Called for each streaming token (delta text). Empty string = stream finished.
    using StreamCallback     = std::function<void(const QString &delta)>;
    // Called when the full response is complete (or on error).
    using CompletionCallback = std::function<void(const QString &response, const QString &error)>;

    virtual void complete(const QList<ChatMessage> &messages,
                          const QString &model,
                          double temperature,
                          int maxTokens,
                          CompletionCallback callback,
                          StreamCallback streamCallback = nullptr) = 0;

    // Cancel any in-flight request.
    virtual void cancel() = 0;

    // Configure the provider (base URL, API key, etc.)
    virtual void setBaseUrl(const QString &url) = 0;
    virtual void setApiKey(const QString &key)  = 0;
};

} // namespace Qcai2
