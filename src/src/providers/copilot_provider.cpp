#include "copilot_provider.h"
#include "../settings/settings.h"
#include "../util/logger.h"

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
QString sidecar_install_hint_dir(const QString &scriptPath = {})
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
copilot_provider_t::copilot_provider_t(QObject *parent) : QObject(parent)
{
}

/**
 * Stops the sidecar process before the provider is destroyed.
 */
copilot_provider_t::~copilot_provider_t()
{
    stop_sidecar();
}

/**
 * Returns the first sidecar script path that exists on disk.
 */
QString copilot_provider_t::find_sidecar_script() const
{
    // Try configured path first
    if (((!this->sidecar_path.isEmpty() && QFileInfo::exists(this->sidecar_path)) == true))
    {
        return this->sidecar_path;
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
bool copilot_provider_t::ensure_sidecar()
{
    if ((((this->process != nullptr) && this->process->state() == QProcess::Running) == true))
    {
        return true;
    }

    const QString script = this->find_sidecar_script();
    if (script.isEmpty() == true)
    {
        return false;
    }

    QCAI_INFO("Copilot",
              QStringLiteral("Starting sidecar: node=%1 script=%2").arg(this->node_path, script));

    this->process = new QProcess(this);
    this->process->setProcessChannelMode(QProcess::SeparateChannels);

    // Set working directory to sidecar dir (so node_modules are found)
    this->process->setWorkingDirectory(QFileInfo(script).absolutePath());

    connect(this->process, &QProcess::readyReadStandardOutput, this,
            &copilot_provider_t::handle_sidecar_output);

    connect(this->process, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(this->process->readAllStandardError());
        this->last_stderr += err;
        QCAI_DEBUG("Copilot/stderr", err.trimmed());
    });

    connect(this->process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        QCAI_WARN("Copilot", QStringLiteral("Sidecar exited with code %1").arg(exitCode));
        QString detail = this->last_stderr.trimmed();
        if (detail.isEmpty() == true)
        {
            detail = QStringLiteral("exit code %1").arg(exitCode);
        }
        const QString errMsg =
            QStringLiteral("Copilot sidecar stopped: %1\n"
                           "Re-run 'cmake --install' or run 'npm install' in %2 so "
                           "'@github/copilot-sdk' is available.")
                .arg(detail, sidecar_install_hint_dir(this->process->workingDirectory()));
        // Fail all pending callbacks (copy first to avoid re-entrancy invalidation)
        const auto pendingCallbacks = this->pending;
        const auto pendingModelListCallbacks = this->model_list_callbacks;
        this->pending.clear();
        this->stream_callbacks.clear();
        this->progress_callbacks.clear();
        this->model_list_callbacks.clear();
        for (auto it = pendingCallbacks.begin(); it != pendingCallbacks.end(); ++it)
        {
            it.value()({}, errMsg, {});
        }
        for (auto it = pendingModelListCallbacks.begin(); it != pendingModelListCallbacks.end();
             ++it)
        {
            it.value()({}, errMsg);
        }
        this->client_started = false;
        this->last_stderr.clear();
        this->process->deleteLater();
        this->process = nullptr;
    });

    this->process->start(this->node_path, {script});
    if (this->process->waitForStarted(5000) == false)
    {
        QCAI_ERROR("Copilot", QStringLiteral("Failed to start sidecar process"));
        delete this->process;
        this->process = nullptr;
        return false;
    }

    return true;
}

/**
 * Writes one compact JSON request line to the running sidecar process.
 * @param req Request payload to send to the sidecar.
 */
void copilot_provider_t::send_request(const QJsonObject &req)
{
    if (((this->process == nullptr) == true))
    {
        return;
    }
    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
    // Defensive: strip any raw newlines/carriage-returns from compact JSON.
    // Qt's toJson(Compact) should escape them inside strings, but empirical
    // evidence shows raw 0x0A can leak through, breaking the JSON-Lines framing.
    data.replace('\n', QByteArray());
    data.replace('\r', QByteArray());
    data.append('\n');

    const qint64 written = this->process->write(data);
    if (written != data.size())
    {
        QCAI_ERROR("Copilot", QStringLiteral("Pipe write failed: wrote %1 of %2 bytes")
                                  .arg(written)
                                  .arg(data.size()));
        return;
    }
    // Flush to ensure data reaches the sidecar pipe immediately
    if (this->process->waitForBytesWritten(3000) == false)
    {
        QCAI_WARN("Copilot", QStringLiteral("Pipe flush timed out (%1 bytes)").arg(data.size()));
    }
}

/**
 * Consumes JSON Lines output from the sidecar and dispatches callbacks.
 */
void copilot_provider_t::handle_sidecar_output()
{
    if (((this->process == nullptr) == true))
    {
        return;
    }

    this->read_buffer.append(this->process->readAllStandardOutput());

    // Process complete lines
    while (true == true)
    {
        qsizetype idx = this->read_buffer.indexOf('\n');
        if (idx < 0)
        {
            break;
        }

        QByteArray line = this->read_buffer.left(idx).trimmed();
        this->read_buffer.remove(0, idx + 1);

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
                this->client_started = true;
            }
            continue;
        }

        auto modelListIt = this->model_list_callbacks.find(id);
        if (((modelListIt != this->model_list_callbacks.end()) == true))
        {
            model_list_callback_t cb = modelListIt.value();
            this->model_list_callbacks.erase(modelListIt);

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

        auto it = this->pending.find(id);
        if (((it == this->pending.end()) == true))
        {
            continue;
        }

        if (((obj.contains(QStringLiteral("progress"))) == true))
        {
            auto progressIt = this->progress_callbacks.find(id);
            if (((progressIt != this->progress_callbacks.end()) == true))
            {
                const QJsonObject progress = obj.value(QStringLiteral("progress")).toObject();
                QString toolName = progress.value(QStringLiteral("toolName")).toString();
                QString message = progress.value(QStringLiteral("message")).toString();
                const QString kind = progress.value(QStringLiteral("kind")).toString();

                provider_raw_event_kind_t rawKind = provider_raw_event_kind_t::REQUEST_STARTED;
                if (kind == QStringLiteral("reasoning_delta"))
                {
                    rawKind = provider_raw_event_kind_t::REASONING_DELTA;
                }
                else if (kind == QStringLiteral("message_delta"))
                {
                    rawKind = provider_raw_event_kind_t::MESSAGE_DELTA;
                }
                else if (kind == QStringLiteral("tool_started"))
                {
                    rawKind = provider_raw_event_kind_t::TOOL_STARTED;
                }
                else if (kind == QStringLiteral("tool_completed"))
                {
                    rawKind = provider_raw_event_kind_t::TOOL_COMPLETED;
                }
                else if (kind == QStringLiteral("response_completed"))
                {
                    rawKind = provider_raw_event_kind_t::RESPONSE_COMPLETED;
                }
                else if (kind == QStringLiteral("idle"))
                {
                    rawKind = provider_raw_event_kind_t::IDLE;
                }
                else if (kind == QStringLiteral("error"))
                {
                    rawKind = provider_raw_event_kind_t::ERROR_EVENT;
                }

                progressIt.value()(provider_raw_event_t{
                    rawKind, this->id(), progress.value(QStringLiteral("rawType")).toString(),
                    toolName, message});
            }
            continue;
        }

        // Handle streaming delta
        if (((obj.contains(QStringLiteral("stream_delta"))) == true))
        {
            auto sit = this->stream_callbacks.find(id);
            if (((sit != this->stream_callbacks.end()) == true))
            {
                sit.value()(obj.value(QStringLiteral("stream_delta")).toString());
            }
            continue;  // don't remove from pending yet
        }

        completion_callback_t cb = it.value();
        this->pending.erase(it);
        this->progress_callbacks.remove(id);

        // Signal stream end
        auto sit = this->stream_callbacks.find(id);
        if (((sit != this->stream_callbacks.end()) == true))
        {
            sit.value()({});  // empty = stream finished
            this->stream_callbacks.erase(sit);
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
            const provider_usage_t usage = provider_usage_from_response_object(result);
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
 * @param max_tokens Maximum completion token count.
 * @param reasoning_effort Optional reasoning hint forwarded to the sidecar.
 * @param callback Receives the final response text or an error.
 * @param stream_callback Receives streamed deltas; an empty string ends the stream.
 */
void copilot_provider_t::complete(const QList<chat_message_t> &messages, const QString &model,
                                  double temperature, int max_tokens,
                                  const QString &reasoning_effort, completion_callback_t callback,
                                  stream_callback_t stream_callback,
                                  progress_callback_t progress_callback)
{
    if (this->ensure_sidecar() == false)
    {
        const QString sidecar_dir = sidecar_install_hint_dir();
        callback({},
                 QStringLiteral("Failed to start Copilot sidecar.\n"
                                "1) Ensure Node.js is installed and available in PATH.\n"
                                "2) Re-run 'cmake --install' or run 'npm install' in: %1\n"
                                "3) Run 'copilot /login' to authenticate.")
                     .arg(sidecar_dir),
                 {});
        return;
    }

    // Send "start" if not yet initialized
    if (this->client_started == false)
    {
        QJsonObject startReq;
        startReq[QStringLiteral("id")] = 0;
        startReq[QStringLiteral("method")] = QStringLiteral("start");
        this->send_request(startReq);
        this->client_started = true;  // optimistic; errors will come back
    }

    // Build messages JSON array
    QJsonArray msgArr;
    for (const auto &m : messages)
    {
        msgArr.append(m.to_json());
    }

    QJsonObject params;
    params[QStringLiteral("messages")] = msgArr;
    params[QStringLiteral("model")] = model;
    params[QStringLiteral("temperature")] = temperature;
    params[QStringLiteral("max_tokens")] = max_tokens;
    // Always use streaming mode — non-streaming sendAndWait has long delays
    params[QStringLiteral("streaming")] = true;
    params[QStringLiteral("completionTimeoutSec")] = settings().copilot_completion_timeout_sec;
    if (!reasoning_effort.isEmpty() && reasoning_effort != QStringLiteral("off"))
    {
        params[QStringLiteral("reasoning_effort")] = reasoning_effort;
    }

    int id = this->next_id++;
    QJsonObject req;
    req[QStringLiteral("id")] = id;
    req[QStringLiteral("method")] = QStringLiteral("complete");
    req[QStringLiteral("params")] = params;

    const qint64 payloadSize = QJsonDocument(req).toJson(QJsonDocument::Compact).size();
    QCAI_DEBUG(
        "Copilot",
        QStringLiteral("Sending request #%1: model=%2 msgs=%3 streaming=%4 payload=%5 bytes "
                       "timeout=%6")
            .arg(id)
            .arg(model)
            .arg(messages.size())
            .arg(stream_callback ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(payloadSize)
            .arg(settings().copilot_completion_timeout_sec > 0
                     ? QStringLiteral("%1s").arg(settings().copilot_completion_timeout_sec)
                     : QStringLiteral("none")));

    this->pending.insert(id, callback);
    if (stream_callback)
    {
        this->stream_callbacks.insert(id, stream_callback);
    }
    if (progress_callback)
    {
        this->progress_callbacks.insert(id, progress_callback);
    }
    this->send_request(req);
}

/**
 * Requests the list of models exposed by the sidecar.
 * @param callback Receives available model ids or an error string.
 */
void copilot_provider_t::list_models(const model_list_callback_t &callback)
{
    if (!this->ensure_sidecar())
    {
        const QString sidecar_dir = sidecar_install_hint_dir();
        callback({}, QStringLiteral("Failed to start Copilot sidecar.\n"
                                    "1) Ensure Node.js is installed and available in PATH.\n"
                                    "2) Re-run 'cmake --install' or run 'npm install' in: %1\n"
                                    "3) Run 'copilot /login' to authenticate.")
                         .arg(sidecar_dir));
        return;
    }

    if (!this->client_started)
    {
        QJsonObject startReq;
        startReq[QStringLiteral("id")] = 0;
        startReq[QStringLiteral("method")] = QStringLiteral("start");
        this->send_request(startReq);
        this->client_started = true;
    }

    const int id = this->next_id++;
    QCAI_DEBUG("Copilot", QStringLiteral("Requesting available models (request #%1)").arg(id));
    QJsonObject req;
    req[QStringLiteral("id")] = id;
    req[QStringLiteral("method")] = QStringLiteral("list_models");

    this->model_list_callbacks.insert(id, callback);
    this->send_request(req);
}

/**
 * Cancels all queued provider callbacks and forwards cancel to the sidecar.
 */
void copilot_provider_t::cancel()
{
    // Fail all pending callbacks on the plugin side (copy first for re-entrancy safety)
    const auto pendingCallbacks = this->pending;
    this->pending.clear();
    this->stream_callbacks.clear();
    this->progress_callbacks.clear();
    for (auto it = pendingCallbacks.begin(); it != pendingCallbacks.end(); ++it)
    {
        it.value()({}, QStringLiteral("Cancelled"), {});
    }

    // Tell sidecar to cancel too (drop queued/active requests)
    if ((this->process != nullptr) && this->process->state() == QProcess::Running)
    {
        QJsonObject req;
        req[QStringLiteral("id")] = this->next_id++;
        req[QStringLiteral("method")] = QStringLiteral("cancel");
        this->send_request(req);
    }
}

/**
 * Stops the sidecar process and clears all pending provider state.
 */
void copilot_provider_t::stop_sidecar()
{
    if (this->process == nullptr)
    {
        return;
    }

    // Send stop command
    QJsonObject req;
    req[QStringLiteral("id")] = 0;
    req[QStringLiteral("method")] = QStringLiteral("stop");
    this->send_request(req);

    this->process->waitForFinished(3000);
    if ((this->process != nullptr) && this->process->state() == QProcess::Running)
    {
        this->process->kill();
        this->process->waitForFinished(1000);
    }
    delete this->process;
    this->process = nullptr;
    this->client_started = false;
    this->pending.clear();
    this->model_list_callbacks.clear();
}

}  // namespace qcai2
