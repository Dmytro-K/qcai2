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
class CopilotProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    /**
     * Creates a provider with auto-detected sidecar settings.
     * @param parent Parent QObject that owns this instance.
     */
    explicit CopilotProvider(QObject *parent = nullptr);

    /**
     * Stops the sidecar process and clears pending requests.
     */
    ~CopilotProvider() override;

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
    QString displayName() const override
    {
        return QStringLiteral("GitHub Copilot");
    }

    /**
     * Sends a completion request through the sidecar.
     * @param messages Conversation history forwarded to the sidecar.
     * @param model Copilot model identifier.
     * @param temperature Sampling temperature.
     * @param maxTokens Maximum completion token count.
     * @param reasoningEffort Optional reasoning hint forwarded to the sidecar.
     * @param callback Receives the final response text or an error.
     * @param streamCallback Receives streamed deltas; an empty string ends the stream.
     */
    void complete(const QList<ChatMessage> &messages, const QString &model, double temperature,
                  int maxTokens, const QString &reasoningEffort, CompletionCallback callback,
                  StreamCallback streamCallback = nullptr) override;

    /**
     * Receives the model list or an error returned by the sidecar.
     * @param models Model identifiers to store.
     * @param error Error text.
     */
    using ModelListCallback = std::function<void(const QStringList &models, const QString &error)>;

    /**
     * Requests the available Copilot models from the sidecar.
     * @param callback Callback invoked with the final result.
     */
    void listModels(const ModelListCallback& callback);

    /**
     * Cancels queued or active requests on both plugin and sidecar sides.
     */
    void cancel() override;

    /**
     * Ignored because the Copilot sidecar manages its own service URL.
     * @param url Base URL to use.
     */
    void setBaseUrl(const QString &url) override
    {
        Q_UNUSED(url);
    }

    /**
     * Ignored because authentication is handled outside this provider.
     * @param key API key or access token.
     */
    void setApiKey(const QString &key) override
    {
        Q_UNUSED(key);
    }

    /**
     * Sets an explicit sidecar script path; empty keeps auto-detection enabled.
     * @param path Path value.
     */
    void setSidecarPath(const QString &path)
    {
        m_sidecarPath = path;
    }

    /**
     * Sets the Node.js executable used to launch the sidecar.
     * @param path Path value.
     */
    void setNodePath(const QString &path)
    {
        m_nodePath = path;
    }

private:
    /**
     * Ensures the sidecar process is running and ready for requests.
     */
    bool ensureSidecar();

    /**
     * Sends a JSON Lines request to the sidecar process.
     * @param req Request payload to send to the sidecar.
     */
    void sendRequest(const QJsonObject &req);

    /**
     * Parses and dispatches sidecar output lines.
     */
    void handleSidecarOutput();

    /**
     * Stops the sidecar process and resets provider state.
     */
    void stopSidecar();

    /**
     * Finds the sidecar script using configured and standard installation paths.
     */
    QString findSidecarScript() const;

    /** Running sidecar process, or null when not started. */
    QProcess *m_process = nullptr;

    /** Explicit sidecar script path, if configured. */
    QString m_sidecarPath;

    /** Node.js executable used to launch the sidecar. */
    QString m_nodePath = QStringLiteral("node");

    /** Next JSON-RPC-style request identifier. */
    int m_nextId = 1;

    /** True after the sidecar has accepted a start request. */
    bool m_clientStarted = false;

    /** Final-response callbacks keyed by request id. */
    QMap<int, CompletionCallback> m_pending;

    /** Streaming callbacks keyed by request id. */
    QMap<int, StreamCallback> m_streamCallbacks;

    /** Model-list callbacks keyed by request id. */
    QMap<int, ModelListCallback> m_modelListCallbacks;

    /** Partial stdout data waiting for a full JSON line. */
    QByteArray m_readBuffer;

    /** Most recent stderr output collected from the sidecar. */
    QString m_lastStderr;

};

}  // namespace qcai2
