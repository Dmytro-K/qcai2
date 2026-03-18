#pragma once

#include "../models/AgentMessages.h"
#include "../progress/AgentProgress.h"
#include "ProviderUsage.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace qcai2
{

/**
 * Abstract interface implemented by AI model providers.
 */
class IAIProvider
{
public:
    /**
     * Destroys the provider interface.
     */
    virtual ~IAIProvider() = default;

    /**
     * Returns the stable provider identifier, such as "openai" or "ollama".
     */
    virtual QString id() const = 0;

    /**
     * Returns the user-facing provider name.
     */
    virtual QString displayName() const = 0;

    /**
     * Receives streamed output chunks; an empty string marks the end of the stream.
     * @param delta Streamed text chunk from the provider.
     */
    using StreamCallback = std::function<void(const QString &delta)>;

    /**
     * Receives the final response text, an error message, and optional usage metadata.
     * @param response Provider response text.
     * @param error Error text.
     * @param usage Provider token-usage counters when available.
     */
    using CompletionCallback = std::function<void(const QString &response, const QString &error,
                                                  const ProviderUsage &usage)>;

    /**
     * Receives provider-specific safe raw progress events.
     * @param event Safe provider event metadata exposed for progress tracking.
     */
    using ProgressCallback = std::function<void(const ProviderRawEvent &event)>;

    /**
     * Starts a completion request.
     * @param messages Conversation history to send to the provider.
     * @param model Provider-specific model identifier.
     * @param temperature Sampling temperature.
     * @param maxTokens Maximum completion token count.
     * @param reasoningEffort Optional reasoning hint for providers that support it.
     * @param callback Called once with the full response or an error.
     * @param streamCallback Called for streamed deltas when streaming is available.
     * @param progressCallback Called for provider-specific safe raw progress events.
     */
    virtual void complete(const QList<ChatMessage> &messages, const QString &model,
                          double temperature, int maxTokens, const QString &reasoningEffort,
                          CompletionCallback callback, StreamCallback streamCallback = nullptr,
                          ProgressCallback progressCallback = nullptr) = 0;

    /**
     * Cancels any in-flight request owned by the provider.
     */
    virtual void cancel() = 0;

    /**
     * Sets the provider base URL when the backend supports custom endpoints.
     * @param url Base URL to use.
     */
    virtual void setBaseUrl(const QString &url) = 0;

    /**
     * Sets the provider API key or access token.
     * @param key API key or access token.
     */
    virtual void setApiKey(const QString &key) = 0;
};

}  // namespace qcai2
