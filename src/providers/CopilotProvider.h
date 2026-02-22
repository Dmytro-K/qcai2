#pragma once

#include "IAIProvider.h"

#include <QObject>
#include <QProcess>
#include <QMap>
#include <functional>

namespace Qcai2 {

// Provider for GitHub Copilot via Node.js sidecar using @github/copilot-sdk.
// Spawns a child Node.js process and communicates over JSON Lines (stdin/stdout).
class CopilotProvider : public QObject, public IAIProvider
{
    Q_OBJECT
public:
    explicit CopilotProvider(QObject *parent = nullptr);
    ~CopilotProvider() override;

    QString id() const override { return QStringLiteral("copilot"); }
    QString displayName() const override { return QStringLiteral("GitHub Copilot"); }

    void complete(const QList<ChatMessage> &messages,
                  const QString &model,
                  double temperature,
                  int maxTokens,
                  CompletionCallback callback,
                  StreamCallback streamCallback = nullptr) override;

    void cancel() override;

    void setBaseUrl(const QString &url) override { Q_UNUSED(url); }
    void setApiKey(const QString &key) override  { Q_UNUSED(key); }

    // Path to sidecar script (auto-detected if empty)
    void setSidecarPath(const QString &path) { m_sidecarPath = path; }
    void setNodePath(const QString &path)    { m_nodePath = path; }

private:
    bool ensureSidecar();
    void sendRequest(const QJsonObject &req);
    void handleSidecarOutput();
    void stopSidecar();
    QString findSidecarScript() const;

    QProcess *m_process = nullptr;
    QString m_sidecarPath;
    QString m_nodePath = QStringLiteral("node");
    int m_nextId = 1;
    bool m_clientStarted = false;

    // Pending callbacks keyed by request id
    QMap<int, CompletionCallback> m_pending;
    QMap<int, StreamCallback> m_streamCallbacks;
    QByteArray m_readBuffer;
    QString m_lastStderr;
};

} // namespace Qcai2
