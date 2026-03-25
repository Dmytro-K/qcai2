#pragma once

#include "../models/agent_messages.h"
#include "../progress/agent_progress.h"
#include "provider_usage.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace qcai2
{

/**
 * Abstract interface implemented by AI model providers.
 */
class iai_provider_t
{
public:
    /**
     * Destroys the provider interface.
     */
    virtual ~iai_provider_t() = default;

    /**
     * Returns the stable provider identifier, such as "openai" or "ollama".
     */
    virtual QString id() const = 0;

    /**
     * Returns the user-facing provider name.
     */
    virtual QString display_name() const = 0;

    /**
     * Receives streamed output chunks; an empty string marks the end of the stream.
     * @param delta Streamed text chunk from the provider.
     */
    using stream_callback_t = std::function<void(const QString &delta)>;

    /**
     * Receives the final response text, an error message, and optional usage metadata.
     * @param response Provider response text.
     * @param error Error text.
     * @param usage Provider token-usage counters when available.
     */
    using completion_callback_t = std::function<void(const QString &response, const QString &error,
                                                     const provider_usage_t &usage)>;

    /**
     * Receives provider-specific safe raw progress events.
     * @param event Safe provider event metadata exposed for progress tracking.
     */
    using progress_callback_t = std::function<void(const provider_raw_event_t &event)>;

    /**
     * Starts a completion request.
     * @param messages Conversation history to send to the provider.
     * @param model Provider-specific model identifier.
     * @param temperature Sampling temperature.
     * @param max_tokens Maximum completion token count.
     * @param reasoning_effort Optional reasoning hint for providers that support it.
     * @param callback Called once with the full response or an error.
     * @param stream_callback Called for streamed deltas when streaming is available.
     * @param progress_callback Called for provider-specific safe raw progress events.
     */
    virtual void complete(const QList<chat_message_t> &messages, const QString &model,
                          double temperature, int max_tokens, const QString &reasoning_effort,
                          completion_callback_t callback,
                          stream_callback_t stream_callback = nullptr,
                          progress_callback_t progress_callback = nullptr) = 0;

    /**
     * Returns true when the provider can accept image attachments in chat messages.
     */
    virtual bool supports_image_input() const
    {
        return false;
    }

    /**
     * Returns an explicit unsupported-reason for one attachment, or an empty string when allowed.
     */
    virtual QString attachment_support_error(const file_attachment_t &attachment) const
    {
        Q_UNUSED(attachment);
        return QStringLiteral("The current provider '%1' does not support file attachments.")
            .arg(this->display_name());
    }

    /**
     * Cancels any in-flight request owned by the provider.
     */
    virtual void cancel() = 0;

    /**
     * Sets the provider base URL when the backend supports custom endpoints.
     * @param url Base URL to use.
     */
    virtual void set_base_url(const QString &url) = 0;

    /**
     * Sets the provider API key or access token.
     * @param key API key or access token.
     */
    virtual void set_api_key(const QString &key) = 0;
};

}  // namespace qcai2
