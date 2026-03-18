#pragma once

#include "IAIProvider.h"

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <functional>

namespace qcai2
{

/**
 * Provider that proxies GitHub Copilot requests through the Node.js sidecar.
 */
class copilot_provider_t : public QObject, public iai_provider_t
{
    Q_OBJECT
public:
    /**
     * Creates a provider with auto-detected sidecar settings.
     * @param parent Parent QObject that owns this instance.
     */
    explicit copilot_provider_t(QObject *parent = nullptr);

    /**
     * Stops the sidecar process and clears pending requests.
     */
    ~copilot_provider_t() override;

    /**
     * Returns the provider identifier used in configuration.
     */
    QString id() const override
    {
        return QStringLiteral("copilot");
    }

    /**
     * Returns the user-visible provider name.
     */
    QString display_name() const override
    {
        return QStringLiteral("GitHub Copilot");
    }

    /**
     * Sends a completion request through the sidecar.
     * @param messages Conversation history forwarded to the sidecar.
     * @param model Copilot model identifier.
     * @param temperature Sampling temperature.
     * @param max_tokens Maximum completion token count.
     * @param reasoning_effort Optional reasoning hint forwarded to the sidecar.
     * @param callback Receives the final response text or an error.
     * @param stream_callback Receives streamed deltas; an empty string ends the stream.
     */
    void complete(const QList<chat_message_t> &messages, const QString &model, double temperature,
                  int max_tokens, const QString &reasoning_effort, completion_callback_t callback,
                  stream_callback_t stream_callback = nullptr,
                  progress_callback_t progress_callback = nullptr) override;

    /**
     * Receives the model list or an error returned by the sidecar.
     * @param models Model identifiers to store.
     * @param error Error text.
     */
    using model_list_callback_t =
        std::function<void(const QStringList &models, const QString &error)>;

    /**
     * Requests the available Copilot models from the sidecar.
     * @param callback Callback invoked with the final result.
     */
    void list_models(const model_list_callback_t &callback);

    /**
     * Cancels queued or active requests on both plugin and sidecar sides.
     */
    void cancel() override;

    /**
     * Ignored because the Copilot sidecar manages its own service URL.
     * @param url Base URL to use.
     */
    void set_base_url(const QString &url) override
    {
        Q_UNUSED(url);
    }

    /**
     * Ignored because authentication is handled outside this provider.
     * @param key API key or access token.
     */
    void set_api_key(const QString &key) override
    {
        Q_UNUSED(key);
    }

    /**
     * Sets an explicit sidecar script path; empty keeps auto-detection enabled.
     * @param path Path value.
     */
    void set_sidecar_path(const QString &path)
    {
        this->sidecar_path = path;
    }

    /**
     * Sets the Node.js executable used to launch the sidecar.
     * @param path Path value.
     */
    void set_node_path(const QString &path)
    {
        this->node_path = path;
    }

private:
    /**
     * Ensures the sidecar process is running and ready for requests.
     */
    bool ensure_sidecar();

    /**
     * Sends a JSON Lines request to the sidecar process.
     * @param req Request payload to send to the sidecar.
     */
    void send_request(const QJsonObject &req);

    /**
     * Parses and dispatches sidecar output lines.
     */
    void handle_sidecar_output();

    /**
     * Stops the sidecar process and resets provider state.
     */
    void stop_sidecar();

    /**
     * Finds the sidecar script using configured and standard installation paths.
     */
    QString find_sidecar_script() const;

    /** Running sidecar process, or null when not started. */
    QProcess *process = nullptr;

    /** Explicit sidecar script path, if configured. */
    QString sidecar_path;

    /** Node.js executable used to launch the sidecar. */
    QString node_path = QStringLiteral("node");

    /** Next JSON-RPC-style request identifier. */
    int next_id = 1;

    /** True after the sidecar has accepted a start request. */
    bool client_started = false;

    /** Final-response callbacks keyed by request id. */
    QMap<int, completion_callback_t> pending;

    /** Streaming callbacks keyed by request id. */
    QMap<int, stream_callback_t> stream_callbacks;

    /** Raw provider progress callbacks keyed by request id. */
    QMap<int, progress_callback_t> progress_callbacks;

    /** Model-list callbacks keyed by request id. */
    QMap<int, model_list_callback_t> model_list_callbacks;

    /** Partial stdout data waiting for a full JSON line. */
    QByteArray read_buffer;

    /** Most recent stderr output collected from the sidecar. */
    QString last_stderr;
};

}  // namespace qcai2
