#include "CopilotProvider.h"
#include "../util/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace qcai2
{

namespace
{

/**
 * Returns the Qt Creator version segment used in per-version plugin paths.
 */
QString qtCreatorVersionSegment()
{
    return QStringLiteral(QCAI2_QTCREATOR_IDE_VERSION);
}

/**
 * Returns the per-user plugin installation root for the current platform.
 */
QString defaultUserPluginRoot()
{
#if defined(Q_OS_WIN)
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        return QDir::cleanPath(localAppData + QStringLiteral("/QtProject/qtcreator/plugins/") +
                               qtCreatorVersionSegment());
    return QDir::cleanPath(QDir::homePath() +
                           QStringLiteral("/AppData/Local/QtProject/qtcreator/plugins/") +
                           qtCreatorVersionSegment());
#elif defined(Q_OS_MACOS)
    return QDir::cleanPath(
        QDir::homePath() +
        QStringLiteral("/Library/Application Support/QtProject/Qt Creator/plugins/") +
        qtCreatorVersionSegment());
#else
    const QString xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
    const QString dataRoot =
        xdgDataHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : xdgDataHome;
    return QDir::cleanPath(dataRoot + QStringLiteral("/data/QtProject/qtcreator/plugins/") +
                           qtCreatorVersionSegment());
#endif
}

/**
 * Returns the default sidecar script path inside the user plugin tree.
 */
QString defaultUserSidecarScriptPath()
{
    return QDir(defaultUserPluginRoot())
        .filePath(QStringLiteral("qcai2/sidecar/copilot-sidecar.js"));
}

/**
 * Returns the absolute path of the currently loaded plugin library.
 */
QString currentPluginLibraryPath()
{
#if defined(Q_OS_WIN)
    HMODULE module = nullptr;
    const auto *address = reinterpret_cast<LPCSTR>(&currentPluginLibraryPath);
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            address, &module))
    {
        return {};
    }

    wchar_t buffer[4096];
    constexpr DWORD bufferSize = sizeof(buffer) / sizeof(buffer[0]);
    const DWORD size = GetModuleFileNameW(module, buffer, bufferSize);
    if (size == 0 || size == bufferSize)
        return {};

    return QDir::fromNativeSeparators(QString::fromWCharArray(buffer, int(size)));
#else
    Dl_info info{};
    if (((dladdr(reinterpret_cast<const void *>(&currentPluginLibraryPath), &info) == 0 ||
          (info.dli_fname == nullptr)) == true))
    {
        return {};
    }

    return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absoluteFilePath();
#endif
}

/**
 * Returns the sidecar script path relative to an installed plugin library.
 */
QString installedSidecarScriptPath()
{
    const QString pluginLibraryPath = currentPluginLibraryPath();
    if (pluginLibraryPath.isEmpty() == true)
    {
        return {};
    }

    return QDir(QFileInfo(pluginLibraryPath).absolutePath())
        .filePath(QStringLiteral(QCAI2_INSTALLED_SIDECAR_RELATIVE_PATH));
}

/**
 * Returns the directory shown in sidecar installation guidance messages.
 * @param scriptPath Sidecar script path selected by the caller.
 */
QString sidecarInstallHintDir(const QString &scriptPath = {})
{
    if (scriptPath.isEmpty() == false)
    {
        return QFileInfo(scriptPath).absolutePath();
    }

    const QString installedScriptPath = installedSidecarScriptPath();
    if (installedScriptPath.isEmpty() == false)
    {
        return QFileInfo(installedScriptPath).absolutePath();
    }

    return QFileInfo(defaultUserSidecarScriptPath()).absolutePath();
}

}  // namespace

/**
 * Creates a Copilot provider with deferred sidecar startup.
 * @param parent Parent QObject that owns this instance.
 */
CopilotProvider::CopilotProvider(QObject *parent) : QObject(parent)
{
}

/**
 * Stops the sidecar process before the provider is destroyed.
 */
CopilotProvider::~CopilotProvider()
{
    stopSidecar();
}

/**
 * Returns the first sidecar script path that exists on disk.
 */
QString CopilotProvider::findSidecarScript() const
{
    // Try configured path first
    if (((!m_sidecarPath.isEmpty() && QFileInfo::exists(m_sidecarPath)) == true))
    {
        return m_sidecarPath;
    }

    // Look relative to the plugin binary location
    QStringList candidates;
    const QString installedSidecar = installedSidecarScriptPath();
    if (installedSidecar.isEmpty() == false)
    {
        candidates.append(installedSidecar);
    }
    candidates.append(defaultUserSidecarScriptPath());
    candidates.append(QDir::cleanPath(
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/../lib/qtcreator/plugins/qcai2/sidecar/copilot-sidecar.js")));
    candidates.append(QStringLiteral(QCAI2_SIDECAR_PATH));  // Development: source tree

    for (const auto &c : candidates)
    {
        QCAI_DEBUG("Copilot", QStringLiteral("Checking sidecar path: %1").arg(c));
        if (QFileInfo::exists(c))
        {
            return c;
        }
    }
    QCAI_WARN("Copilot", QStringLiteral("No sidecar script found"));
    return {};
}

/**
 * Starts the sidecar process on demand and hooks up its signal handlers.
 */
bool CopilotProvider::ensureSidecar()
{
    if ((((m_process != nullptr) && m_process->state() == QProcess::Running) == true))
    {
        return true;
    }

    const QString script = findSidecarScript();
    if (script.isEmpty() == true)
    {
        return false;
    }

    QCAI_INFO("Copilot",
              QStringLiteral("Starting sidecar: node=%1 script=%2").arg(m_nodePath, script));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // Set working directory to sidecar dir (so node_modules are found)
    m_process->setWorkingDirectory(QFileInfo(script).absolutePath());

    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &CopilotProvider::handleSidecarOutput);

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(m_process->readAllStandardError());
        m_lastStderr += err;
        QCAI_DEBUG("Copilot/stderr", err.trimmed());
    });

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        QCAI_WARN("Copilot", QStringLiteral("Sidecar exited with code %1").arg(exitCode));
        QString detail = m_lastStderr.trimmed();
        if (detail.isEmpty() == true)
        {
            detail = QStringLiteral("exit code %1").arg(exitCode);
        }
        const QString errMsg =
            QStringLiteral("Copilot sidecar stopped: %1\n"
                           "Re-run 'cmake --install' or run 'npm install' in %2 so "
                           "'@github/copilot-sdk' is available.")
                .arg(detail, sidecarInstallHintDir(m_process->workingDirectory()));
        // Fail all pending callbacks (copy first to avoid re-entrancy invalidation)
        const auto pendingCallbacks = m_pending;
        const auto pendingModelListCallbacks = m_modelListCallbacks;
        m_pending.clear();
        m_streamCallbacks.clear();
        m_modelListCallbacks.clear();
        for (auto it = pendingCallbacks.begin(); it != pendingCallbacks.end(); ++it)
        {
            it.value()({}, errMsg, {});
        }
        for (auto it = pendingModelListCallbacks.begin(); it != pendingModelListCallbacks.end();
             ++it)
        {
            it.value()({}, errMsg);
        }
        m_clientStarted = false;
        m_lastStderr.clear();
        m_process->deleteLater();
        m_process = nullptr;
    });

    m_process->start(m_nodePath, {script});
    if (m_process->waitForStarted(5000) == false)
    {
        QCAI_ERROR("Copilot", QStringLiteral("Failed to start sidecar process"));
        delete m_process;
        m_process = nullptr;
        return false;
    }

    return true;
}

/**
 * Writes one compact JSON request line to the running sidecar process.
 * @param req Request payload to send to the sidecar.
 */
void CopilotProvider::sendRequest(const QJsonObject &req)
{
    if (((m_process == nullptr) == true))
    {
        return;
    }
    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_process->write(data);
}

/**
 * Consumes JSON Lines output from the sidecar and dispatches callbacks.
 */
void CopilotProvider::handleSidecarOutput()
{
    if (((m_process == nullptr) == true))
    {
        return;
    }

    m_readBuffer.append(m_process->readAllStandardOutput());

    // Process complete lines
    while (true == true)
    {
        qsizetype idx = m_readBuffer.indexOf('\n');
        if (idx < 0)
        {
            break;
        }

        QByteArray line = m_readBuffer.left(idx).trimmed();
        m_readBuffer.remove(0, idx + 1);

        if (line.isEmpty() == true)
        {
            continue;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (((err.error != QJsonParseError::NoError) == true))
        {
            continue;
        }

        QJsonObject obj = doc.object();
        int id = obj.value(QStringLiteral("id")).toInt(-1);

        // Handle the initial sidecar_ready signal
        if (((id == 0) == true))
        {
            // Check if this is the "start" response
            QJsonObject res = obj.value(QStringLiteral("result")).toObject();
            if (((res.value(QStringLiteral("status")).toString() == QStringLiteral("ready")) ==
                 true))
            {
                m_clientStarted = true;
            }
            continue;
        }

        auto modelListIt = m_modelListCallbacks.find(id);
        if (((modelListIt != m_modelListCallbacks.end()) == true))
        {
            ModelListCallback cb = modelListIt.value();
            m_modelListCallbacks.erase(modelListIt);

            if (((obj.contains(QStringLiteral("error"))) == true))
            {
                const QString error = obj.value(QStringLiteral("error")).toString();
                QCAI_ERROR("Copilot",
                           QStringLiteral("Model request #%1 error: %2").arg(id).arg(error));
                cb({}, error);
            }
            else
            {
                const QJsonArray modelArray = obj.value(QStringLiteral("result"))
                                                  .toObject()
                                                  .value(QStringLiteral("models"))
                                                  .toArray();
                QStringList models;
                models.reserve(modelArray.size());
                for (const auto &value : modelArray)
                {
                    if (value.isString())
                    {
                        models.append(value.toString());
                    }
                    else if (value.isObject())
                    {
                        models.append(value.toObject().value(QStringLiteral("id")).toString());
                    }
                }
                QCAI_INFO("Copilot",
                          QStringLiteral("Loaded %1 models from sidecar").arg(models.size()));
                cb(models, {});
            }
            continue;
        }

        auto it = m_pending.find(id);
        if (((it == m_pending.end()) == true))
        {
            continue;
        }

        // Handle streaming delta
        if (((obj.contains(QStringLiteral("stream_delta"))) == true))
        {
            auto sit = m_streamCallbacks.find(id);
            if (((sit != m_streamCallbacks.end()) == true))
            {
                sit.value()(obj.value(QStringLiteral("stream_delta")).toString());
            }
            continue;  // don't remove from pending yet
        }

        CompletionCallback cb = it.value();
        m_pending.erase(it);

        // Signal stream end
        auto sit = m_streamCallbacks.find(id);
        if (((sit != m_streamCallbacks.end()) == true))
        {
            sit.value()({});  // empty = stream finished
            m_streamCallbacks.erase(sit);
        }

        if (((obj.contains(QStringLiteral("error"))) == true))
        {
            QCAI_ERROR("Copilot", QStringLiteral("Request #%1 error: %2")
                                      .arg(id)
                                      .arg(obj.value(QStringLiteral("error")).toString()));
            cb({}, obj.value(QStringLiteral("error")).toString(), {});
        }
        else
        {
            QJsonObject result = obj.value(QStringLiteral("result")).toObject();
            const ProviderUsage usage = providerUsageFromResponseObject(result);
            QCAI_DEBUG("Copilot",
                       QStringLiteral("Request #%1 complete, %2 chars")
                           .arg(id)
                           .arg(result.value(QStringLiteral("content")).toString().length()));
            cb(result.value(QStringLiteral("content")).toString(), {}, usage);
        }
    }
}

/**
 * Sends a completion request through the Copilot sidecar.
 * @param messages Conversation history forwarded to the sidecar.
 * @param model Copilot model identifier.
 * @param temperature Sampling temperature.
 * @param maxTokens Maximum completion token count.
 * @param reasoningEffort Optional reasoning hint forwarded to the sidecar.
 * @param callback Receives the final response text or an error.
 * @param streamCallback Receives streamed deltas; an empty string ends the stream.
 */
void CopilotProvider::complete(const QList<ChatMessage> &messages, const QString &model,
                               double temperature, int maxTokens, const QString &reasoningEffort,
                               CompletionCallback callback, StreamCallback streamCallback)
{
    if (ensureSidecar() == false)
    {
        const QString sidecarDir = sidecarInstallHintDir();
        callback({},
                 QStringLiteral("Failed to start Copilot sidecar.\n"
                                "1) Ensure Node.js is installed and available in PATH.\n"
                                "2) Re-run 'cmake --install' or run 'npm install' in: %1\n"
                                "3) Run 'copilot /login' to authenticate.")
                     .arg(sidecarDir),
                 {});
        return;
    }

    // Send "start" if not yet initialized
    if (m_clientStarted == false)
    {
        QJsonObject startReq;
        startReq[QStringLiteral("id")] = 0;
        startReq[QStringLiteral("method")] = QStringLiteral("start");
        sendRequest(startReq);
        m_clientStarted = true;  // optimistic; errors will come back
    }

    // Build messages JSON array
    QJsonArray msgArr;
    for (const auto &m : messages)
    {
        msgArr.append(m.toJson());
    }

    QJsonObject params;
    params[QStringLiteral("messages")] = msgArr;
    params[QStringLiteral("model")] = model;
    params[QStringLiteral("temperature")] = temperature;
    params[QStringLiteral("maxTokens")] = maxTokens;
    // Always use streaming mode — non-streaming sendAndWait has long delays
    params[QStringLiteral("streaming")] = true;
    if (!reasoningEffort.isEmpty() && reasoningEffort != QStringLiteral("off"))
    {
        params[QStringLiteral("reasoningEffort")] = reasoningEffort;
    }

    int id = m_nextId++;
    QCAI_DEBUG("Copilot", QStringLiteral("Sending request #%1: model=%2 msgs=%3 streaming=%4")
                              .arg(id)
                              .arg(model)
                              .arg(messages.size())
                              .arg(streamCallback ? QStringLiteral("yes") : QStringLiteral("no")));
    QJsonObject req;
    req[QStringLiteral("id")] = id;
    req[QStringLiteral("method")] = QStringLiteral("complete");
    req[QStringLiteral("params")] = params;

    m_pending.insert(id, callback);
    if (streamCallback)
    {
        m_streamCallbacks.insert(id, streamCallback);
    }
    sendRequest(req);
}

/**
 * Requests the list of models exposed by the sidecar.
 * @param callback Receives available model ids or an error string.
 */
void CopilotProvider::listModels(const ModelListCallback &callback)
{
    if (!ensureSidecar())
    {
        const QString sidecarDir = sidecarInstallHintDir();
        callback({}, QStringLiteral("Failed to start Copilot sidecar.\n"
                                    "1) Ensure Node.js is installed and available in PATH.\n"
                                    "2) Re-run 'cmake --install' or run 'npm install' in: %1\n"
                                    "3) Run 'copilot /login' to authenticate.")
                         .arg(sidecarDir));
        return;
    }

    if (!m_clientStarted)
    {
        QJsonObject startReq;
        startReq[QStringLiteral("id")] = 0;
        startReq[QStringLiteral("method")] = QStringLiteral("start");
        sendRequest(startReq);
        m_clientStarted = true;
    }

    const int id = m_nextId++;
    QCAI_DEBUG("Copilot", QStringLiteral("Requesting available models (request #%1)").arg(id));
    QJsonObject req;
    req[QStringLiteral("id")] = id;
    req[QStringLiteral("method")] = QStringLiteral("list_models");

    m_modelListCallbacks.insert(id, callback);
    sendRequest(req);
}

/**
 * Cancels all queued provider callbacks and forwards cancel to the sidecar.
 */
void CopilotProvider::cancel()
{
    // Fail all pending callbacks on the plugin side (copy first for re-entrancy safety)
    const auto pendingCallbacks = m_pending;
    m_pending.clear();
    m_streamCallbacks.clear();
    for (auto it = pendingCallbacks.begin(); it != pendingCallbacks.end(); ++it)
    {
        it.value()({}, QStringLiteral("Cancelled"), {});
    }

    // Tell sidecar to cancel too (drop queued/active requests)
    if ((m_process != nullptr) && m_process->state() == QProcess::Running)
    {
        QJsonObject req;
        req[QStringLiteral("id")] = m_nextId++;
        req[QStringLiteral("method")] = QStringLiteral("cancel");
        sendRequest(req);
    }
}

/**
 * Stops the sidecar process and clears all pending provider state.
 */
void CopilotProvider::stopSidecar()
{
    if (m_process == nullptr)
    {
        return;
    }

    // Send stop command
    QJsonObject req;
    req[QStringLiteral("id")] = 0;
    req[QStringLiteral("method")] = QStringLiteral("stop");
    sendRequest(req);

    m_process->waitForFinished(3000);
    if ((m_process != nullptr) && m_process->state() == QProcess::Running)
    {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_process;
    m_process = nullptr;
    m_clientStarted = false;
    m_pending.clear();
    m_modelListCallbacks.clear();
}

}  // namespace qcai2
