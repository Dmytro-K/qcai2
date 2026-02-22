#include "CopilotProvider.h"
#include "../util/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace Qcai2 {

CopilotProvider::CopilotProvider(QObject *parent)
    : QObject(parent)
{}

CopilotProvider::~CopilotProvider()
{
    stopSidecar();
}

QString CopilotProvider::findSidecarScript() const
{
    // Try configured path first
    if (!m_sidecarPath.isEmpty() && QFileInfo::exists(m_sidecarPath))
        return m_sidecarPath;

    // Look relative to the plugin binary location
    const QStringList candidates = {
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/../share/qtcreator/qcai2/sidecar/copilot-sidecar.js"),
        QDir::homePath()
            + QStringLiteral("/.local/share/qcai2/sidecar/copilot-sidecar.js"),
        // Development: relative to source tree
        QStringLiteral(QCAI2_SIDECAR_PATH),
    };

    for (const auto &c : candidates) {
        QCAI_DEBUG("Copilot", QStringLiteral("Checking sidecar path: %1").arg(c));
        if (QFileInfo::exists(c))
            return c;
    }
    QCAI_WARN("Copilot", QStringLiteral("No sidecar script found"));
    return {};
}

bool CopilotProvider::ensureSidecar()
{
    if (m_process && m_process->state() == QProcess::Running)
        return true;

    const QString script = findSidecarScript();
    if (script.isEmpty())
        return false;

    QCAI_INFO("Copilot", QStringLiteral("Starting sidecar: node=%1 script=%2").arg(m_nodePath, script));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // Set working directory to sidecar dir (so node_modules are found)
    m_process->setWorkingDirectory(QFileInfo(script).absolutePath());

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &CopilotProvider::handleSidecarOutput);

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(m_process->readAllStandardError());
        m_lastStderr += err;
        QCAI_DEBUG("Copilot/stderr", err.trimmed());
    });

    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) {
        QCAI_WARN("Copilot", QStringLiteral("Sidecar exited with code %1").arg(exitCode));
        QString detail = m_lastStderr.trimmed();
        if (detail.isEmpty())
            detail = QStringLiteral("exit code %1").arg(exitCode);
        const QString errMsg = QStringLiteral("Copilot sidecar stopped: %1\n"
            "Make sure you ran 'npm install' in the sidecar/ directory "
            "and '@github/copilot-sdk' is installed.").arg(detail);
        // Fail all pending callbacks
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
            it.value()({}, errMsg);
        m_pending.clear();
        m_clientStarted = false;
        m_lastStderr.clear();
        m_process->deleteLater();
        m_process = nullptr;
    });

    m_process->start(m_nodePath, {script});
    if (!m_process->waitForStarted(5000)) {
        QCAI_ERROR("Copilot", QStringLiteral("Failed to start sidecar process"));
        delete m_process;
        m_process = nullptr;
        return false;
    }

    return true;
}

void CopilotProvider::sendRequest(const QJsonObject &req)
{
    if (!m_process) return;
    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_process->write(data);
}

void CopilotProvider::handleSidecarOutput()
{
    if (!m_process) return;

    m_readBuffer.append(m_process->readAllStandardOutput());

    // Process complete lines
    while (true) {
        qsizetype idx = m_readBuffer.indexOf('\n');
        if (idx < 0) break;

        QByteArray line = m_readBuffer.left(idx).trimmed();
        m_readBuffer.remove(0, idx + 1);

        if (line.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError)
            continue;

        QJsonObject obj = doc.object();
        int id = obj.value(QStringLiteral("id")).toInt(-1);

        // Handle the initial sidecar_ready signal
        if (id == 0) {
            // Check if this is the "start" response
            QJsonObject res = obj.value(QStringLiteral("result")).toObject();
            if (res.value(QStringLiteral("status")).toString() == QStringLiteral("ready"))
                m_clientStarted = true;
            continue;
        }

        auto it = m_pending.find(id);
        if (it == m_pending.end()) continue;

        // Handle streaming delta
        if (obj.contains(QStringLiteral("stream_delta"))) {
            auto sit = m_streamCallbacks.find(id);
            if (sit != m_streamCallbacks.end()) {
                sit.value()(obj.value(QStringLiteral("stream_delta")).toString());
            }
            continue; // don't remove from pending yet
        }

        CompletionCallback cb = it.value();
        m_pending.erase(it);

        // Signal stream end
        auto sit = m_streamCallbacks.find(id);
        if (sit != m_streamCallbacks.end()) {
            sit.value()({}); // empty = stream finished
            m_streamCallbacks.erase(sit);
        }

        if (obj.contains(QStringLiteral("error"))) {
            QCAI_ERROR("Copilot", QStringLiteral("Request #%1 error: %2")
                .arg(id).arg(obj.value(QStringLiteral("error")).toString()));
            cb({}, obj.value(QStringLiteral("error")).toString());
        } else {
            QJsonObject result = obj.value(QStringLiteral("result")).toObject();
            QCAI_DEBUG("Copilot", QStringLiteral("Request #%1 complete, %2 chars")
                .arg(id).arg(result.value(QStringLiteral("content")).toString().length()));
            cb(result.value(QStringLiteral("content")).toString(), {});
        }
    }
}

void CopilotProvider::complete(const QList<ChatMessage> &messages,
                                const QString &model,
                                double temperature,
                                int maxTokens,
                                CompletionCallback callback,
                                StreamCallback streamCallback)
{
    if (!ensureSidecar()) {
        const QString sidecarDir = QStringLiteral(QCAI2_SIDECAR_PATH);
        callback({}, QStringLiteral("Failed to start Copilot sidecar.\n"
                                     "1) Ensure Node.js is installed and available in PATH.\n"
                                     "2) Run 'npm install' in: %1\n"
                                     "3) Run 'copilot /login' to authenticate.")
                     .arg(QFileInfo(sidecarDir).absolutePath()));
        return;
    }

    // Send "start" if not yet initialized
    if (!m_clientStarted) {
        QJsonObject startReq;
        startReq[QStringLiteral("id")]     = 0;
        startReq[QStringLiteral("method")] = QStringLiteral("start");
        sendRequest(startReq);
        m_clientStarted = true; // optimistic; errors will come back
    }

    // Build messages JSON array
    QJsonArray msgArr;
    for (const auto &m : messages)
        msgArr.append(m.toJson());

    QJsonObject params;
    params[QStringLiteral("messages")]    = msgArr;
    params[QStringLiteral("model")]       = model;
    params[QStringLiteral("temperature")] = temperature;
    params[QStringLiteral("maxTokens")]   = maxTokens;
    if (streamCallback)
        params[QStringLiteral("streaming")] = true;

    int id = m_nextId++;
    QCAI_DEBUG("Copilot", QStringLiteral("Sending request #%1: model=%2 msgs=%3 streaming=%4")
        .arg(id).arg(model).arg(messages.size())
        .arg(streamCallback ? QStringLiteral("yes") : QStringLiteral("no")));
    QJsonObject req;
    req[QStringLiteral("id")]     = id;
    req[QStringLiteral("method")] = QStringLiteral("complete");
    req[QStringLiteral("params")] = params;

    m_pending.insert(id, callback);
    if (streamCallback)
        m_streamCallbacks.insert(id, streamCallback);
    sendRequest(req);
}

void CopilotProvider::cancel()
{
    // Fail all pending callbacks on the plugin side
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        it.value()({}, QStringLiteral("Cancelled"));
    m_pending.clear();
    m_streamCallbacks.clear();

    // Tell sidecar to cancel too (drop queued/active requests)
    if (m_process && m_process->state() == QProcess::Running) {
        QJsonObject req;
        req[QStringLiteral("id")]     = m_nextId++;
        req[QStringLiteral("method")] = QStringLiteral("cancel");
        sendRequest(req);
    }
}

void CopilotProvider::stopSidecar()
{
    if (!m_process) return;

    // Send stop command
    QJsonObject req;
    req[QStringLiteral("id")]     = 0;
    req[QStringLiteral("method")] = QStringLiteral("stop");
    sendRequest(req);

    m_process->waitForFinished(3000);
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_process;
    m_process = nullptr;
    m_clientStarted = false;
    m_pending.clear();
}

} // namespace Qcai2
