/*! Implements runtime MCP tool discovery and invocation for agent runs. */

#include "McpToolManager.h"

#include "../settings/Settings.h"
#include "../tools/ITool.h"
#include "../tools/ToolRegistry.h"
#include "../util/Logger.h"
#include "../util/Migration.h"

#include <qtmcp/Client.h>
#include <qtmcp/HttpTransport.h>
#include <qtmcp/ServerDefinition.h>
#include <qtmcp/StdioTransport.h>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <utility>

namespace qcai2
{

namespace
{

constexpr auto kMcpProtocolVersionStdio = "2024-11-05";
constexpr auto kMcpProtocolVersionHttp = "2025-03-26";
constexpr int kMaxToolsListPages = 32;

QString sanitizeToolSegment(const QString &value)
{
    QString result;
    result.reserve(value.size());

    for (const QChar ch : value)
    {
        if (ch.isLetterOrNumber() == true)
        {
            result.append(ch.toLower());
        }
        else
        {
            result.append(QLatin1Char('_'));
        }
    }

    while (((result.contains(QStringLiteral("__"))) == true))
    {
        result.replace(QStringLiteral("__"), QStringLiteral("_"));
    }

    while (((result.startsWith(QLatin1Char('_'))) == true))
    {
        result.remove(0, 1);
    }
    while (((result.endsWith(QLatin1Char('_'))) == true))
    {
        result.chop(1);
    }

    return result.isEmpty() ? QStringLiteral("tool") : result;
}

QString uniqueExposedToolName(const QString &serverName, const QString &toolName,
                              const QSet<QString> &usedNames)
{
    const QString base = QStringLiteral("%1%2__%3")
                             .arg(QStringLiteral("mcp_"), sanitizeToolSegment(serverName),
                                  sanitizeToolSegment(toolName));
    QString candidate = base;
    int suffix = 2;

    while (usedNames.contains(candidate))
    {
        candidate = QStringLiteral("%1_%2").arg(base).arg(suffix++);
    }

    return candidate;
}

QString prettyJson(const QJsonValue &value)
{
    if (value.isObject() == true)
    {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented))
            .trimmed();
    }
    if (value.isArray() == true)
    {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented))
            .trimmed();
    }
    if (value.isString() == true)
    {
        return value.toString();
    }
    if (value.isBool() == true)
    {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble() == true)
    {
        return QString::number(value.toDouble());
    }
    if (value.isNull() == true)
    {
        return QStringLiteral("null");
    }
    return QString();
}

QString flattenToolContent(const QJsonArray &content)
{
    QStringList parts;

    for (const QJsonValue &entryValue : content)
    {
        if (entryValue.isObject() == false)
        {
            const QString raw = prettyJson(entryValue);
            if (raw.isEmpty() == false)
            {
                parts.append(raw);
            }
            continue;
        }

        const QJsonObject entry = entryValue.toObject();
        const QString type = entry.value(QStringLiteral("type")).toString();

        if (((type == QStringLiteral("text")) == true))
        {
            const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
            if (text.isEmpty() == false)
            {
                parts.append(text);
            }
            continue;
        }

        if (((type == QStringLiteral("image")) == true))
        {
            parts.append(QStringLiteral("[image content, mimeType=%1]")
                             .arg(entry.value(QStringLiteral("mimeType")).toString()));
            continue;
        }

        if (((type == QStringLiteral("resource")) == true))
        {
            const QJsonObject resource = entry.value(QStringLiteral("resource")).toObject();
            const QString uri = resource.value(QStringLiteral("uri")).toString();
            const QString text = resource.value(QStringLiteral("text")).toString().trimmed();
            if (((!uri.isEmpty() && !text.isEmpty()) == true))
            {
                parts.append(QStringLiteral("[resource %1]\n%2").arg(uri, text));
            }
            else if (uri.isEmpty() == false)
            {
                parts.append(QStringLiteral("[resource %1]").arg(uri));
            }
            else
            {
                parts.append(prettyJson(resource));
            }
            continue;
        }

        const QString raw = prettyJson(entry);
        if (raw.isEmpty() == false)
        {
            parts.append(raw);
        }
    }

    return parts.join(QStringLiteral("\n\n")).trimmed();
}

bool jsonRequestIdMatches(const QJsonValue &id, qint64 expectedId)
{
    if (id.isDouble() == true)
    {
        return static_cast<qint64>(id.toDouble()) == expectedId;
    }
    if (id.isString() == true)
    {
        return id.toString() == QString::number(expectedId);
    }
    return false;
}

qtmcp::ServerDefinitions loadProjectServerDefinitions(const QString &projectDir,
                                                      QStringList *messages)
{
    qtmcp::ServerDefinitions definitions;
    if (((projectDir.trimmed().isEmpty()) == true))
    {
        return definitions;
    }

    const QString storagePath = QDir(projectDir).filePath(QStringLiteral(".qcai2/session.json"));
    if (QFileInfo::exists(storagePath) == false)
    {
        return definitions;
    }

    QFile storageFile(storagePath);
    if (storageFile.open(QIODevice::ReadOnly) == false)
    {
        if (((messages != nullptr) == true))
        {
            messages->append(
                QStringLiteral("⚠ MCP: could not read project session MCP config from `%1`: %2")
                    .arg(storagePath, storageFile.errorString()));
        }
        return definitions;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(storageFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (messages != nullptr)
        {
            messages->append(QStringLiteral("⚠ MCP: invalid project session JSON in `%1`: %2")
                                 .arg(storagePath, parseError.errorString()));
        }
        return definitions;
    }

    const QJsonValue mcpServersValue = document.object().value(QStringLiteral("mcpServers"));
    if (mcpServersValue.isUndefined() || mcpServersValue.isNull())
    {
        return definitions;
    }
    if (!mcpServersValue.isObject())
    {
        if (messages != nullptr)
        {
            messages->append(
                QStringLiteral("⚠ MCP: project session key `mcpServers` must be an object."));
        }
        return definitions;
    }

    QString error;
    if (!qtmcp::serverDefinitionsFromJson(mcpServersValue.toObject(), &definitions, &error))
    {
        if (messages != nullptr)
        {
            messages->append(
                QStringLiteral("⚠ MCP: failed to parse project MCP servers from `%1`: %2")
                    .arg(storagePath, error));
        }
        return {};
    }

    return definitions;
}

QString expandEnvironmentPlaceholders(const QString &value, const QString &serverName,
                                      const QString &fieldLabel, const QProcessEnvironment &env,
                                      QStringList *messages)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})"));

    QString resolved = value;
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(value);
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        const QString variableName = match.captured(1);
        if (!env.contains(variableName))
        {
            if (messages != nullptr)
            {
                messages->append(
                    QStringLiteral(
                        "⚠ MCP: server `%1` references unset environment variable `%2` in %3.")
                        .arg(serverName, variableName, fieldLabel));
            }
            continue;
        }

        resolved.replace(match.captured(0), env.value(variableName));
    }

    return resolved;
}

QString extraFieldString(const QJsonObject &extraFields, const QString &name)
{
    return extraFields.value(name).toString().trimmed();
}

bool extraFieldBool(const QJsonObject &extraFields, const QString &name, bool fallback)
{
    const QJsonValue value = extraFields.value(name);
    if (value.isBool())
    {
        return value.toBool();
    }
    return fallback;
}

QStringList extraFieldStringList(const QJsonObject &extraFields, const QString &name)
{
    QStringList values;
    const QJsonValue value = extraFields.value(name);
    if (value.isString())
    {
        const QStringList parts =
            value.toString().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        for (const QString &part : parts)
        {
            values.append(part.trimmed());
        }
        return values;
    }

    if (!value.isArray())
    {
        return values;
    }

    for (const QJsonValue &entry : value.toArray())
    {
        if (entry.isString())
        {
            const QString trimmed = entry.toString().trimmed();
            if (!trimmed.isEmpty())
            {
                values.append(trimmed);
            }
        }
    }
    return values;
}

struct RpcResult
{
    bool finished = false;
    bool protocolError = false;
    QJsonValue result;
    int errorCode = 0;
    QString errorMessage;
    QJsonValue errorData;
};

struct RemoteTool
{
    QString serverName;
    QString toolName;
    QString exposedName;
    QString description;
    QJsonObject inputSchema;
};

struct ServerSession
{
    QString serverName;
    qtmcp::ServerDefinition definition;
    std::unique_ptr<qtmcp::Client> client;
    QString negotiatedProtocolVersion;
    QJsonObject capabilities;
    QMap<QString, RemoteTool> toolsByExposedName;
    QStringList recentLogLines;
};

class McpDynamicTool final : public ITool
{
public:
    McpDynamicTool(McpToolManager *manager, RemoteTool metadata)
        : m_manager(manager), m_metadata(std::move(metadata))
    {
    }

    QString name() const override
    {
        return m_metadata.exposedName;
    }

    QString description() const override
    {
        if (m_metadata.description.trimmed().isEmpty())
        {
            return QStringLiteral("Remote MCP tool `%1` from server `%2`.")
                .arg(m_metadata.toolName, m_metadata.serverName);
        }

        return QStringLiteral("Remote MCP tool `%1` from server `%2`: %3")
            .arg(m_metadata.toolName, m_metadata.serverName, m_metadata.description.trimmed());
    }

    QJsonObject argsSchema() const override
    {
        return m_metadata.inputSchema.isEmpty() ? QJsonObject{{"type", "object"}}
                                                : m_metadata.inputSchema;
    }

    QString execute(const QJsonObject &args, const QString &) override
    {
        if (m_manager == nullptr)
        {
            return QStringLiteral("Error: MCP runtime manager is unavailable.");
        }
        return m_manager->executeTool(m_metadata.exposedName, args);
    }

private:
    QPointer<McpToolManager> m_manager;
    RemoteTool m_metadata;
};

}  // namespace

class McpToolManager::Impl
{
public:
    explicit Impl(ToolRegistry *toolRegistry) : registry(toolRegistry)
    {
    }

    void clearDynamicTools()
    {
        if (registry != nullptr)
        {
            for (const QString &toolName : std::as_const(registeredToolNames))
            {
                registry->unregisterTool(toolName);
            }
        }

        registeredToolNames.clear();

        for (auto it = sessions.begin(); it != sessions.end(); ++it)
        {
            if (it.value()->client != nullptr)
            {
                it.value()->client->stop();
            }
        }
        sessions.clear();
    }

    static bool waitForConnection(qtmcp::Client *client, int timeoutMs, QString *error)
    {
        if (client == nullptr)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("MCP client is null.");
            }
            return false;
        }
        if (client->isConnected())
        {
            return true;
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        bool connected = false;
        QString failure;

        const auto connectedConnection =
            QObject::connect(client, &qtmcp::Client::connected, &loop, [&]() {
                connected = true;
                loop.quit();
            });
        const auto errorConnection = QObject::connect(
            client, &qtmcp::Client::transportErrorOccurred, &loop, [&](const QString &message) {
                failure = message;
                loop.quit();
            });
        const auto disconnectedConnection =
            QObject::connect(client, &qtmcp::Client::disconnected, &loop, [&]() {
                if (!connected)
                {
                    failure = QStringLiteral("MCP server disconnected during startup.");
                }
                loop.quit();
            });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(timeoutMs);
        client->start();
        loop.exec();

        QObject::disconnect(connectedConnection);
        QObject::disconnect(errorConnection);
        QObject::disconnect(disconnectedConnection);

        if (connected)
        {
            return true;
        }

        if (failure.isEmpty())
        {
            failure = QStringLiteral("Timed out waiting for MCP server startup.");
        }
        if (error != nullptr)
        {
            *error = failure;
        }
        return false;
    }

    static RpcResult sendRequestSync(qtmcp::Client *client, const QString &method,
                                     const QJsonValue &params, int timeoutMs)
    {
        RpcResult rpcResult;
        if (client == nullptr)
        {
            rpcResult.protocolError = true;
            rpcResult.errorMessage = QStringLiteral("MCP client is null.");
            return rpcResult;
        }

        const qint64 requestId = client->sendRequest(method, params);
        if (requestId <= 0)
        {
            rpcResult.protocolError = true;
            rpcResult.errorMessage =
                QStringLiteral("Failed to send MCP request `%1`.").arg(method);
            return rpcResult;
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        const auto responseConnection =
            QObject::connect(client, &qtmcp::Client::responseReceived, &loop,
                             [&](const QJsonValue &id, const QJsonValue &result) {
                                 if (!jsonRequestIdMatches(id, requestId))
                                 {
                                     return;
                                 }
                                 rpcResult.finished = true;
                                 rpcResult.result = result;
                                 loop.quit();
                             });
        const auto errorResponseConnection = QObject::connect(
            client, &qtmcp::Client::errorResponseReceived, &loop,
            [&](const QJsonValue &id, int code, const QString &message, const QJsonValue &data) {
                if (!jsonRequestIdMatches(id, requestId))
                {
                    return;
                }
                rpcResult.finished = true;
                rpcResult.protocolError = true;
                rpcResult.errorCode = code;
                rpcResult.errorMessage = message;
                rpcResult.errorData = data;
                loop.quit();
            });
        const auto transportErrorConnection = QObject::connect(
            client, &qtmcp::Client::transportErrorOccurred, &loop, [&](const QString &message) {
                rpcResult.protocolError = true;
                rpcResult.errorMessage = message;
                loop.quit();
            });
        const auto disconnectedConnection =
            QObject::connect(client, &qtmcp::Client::disconnected, &loop, [&]() {
                if (!rpcResult.finished)
                {
                    rpcResult.protocolError = true;
                    rpcResult.errorMessage =
                        QStringLiteral("MCP server disconnected while waiting for `%1`.")
                            .arg(method);
                }
                loop.quit();
            });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(timeoutMs);
        loop.exec();

        QObject::disconnect(responseConnection);
        QObject::disconnect(errorResponseConnection);
        QObject::disconnect(transportErrorConnection);
        QObject::disconnect(disconnectedConnection);

        if (!rpcResult.finished && !rpcResult.protocolError)
        {
            rpcResult.protocolError = true;
            rpcResult.errorMessage = QStringLiteral("MCP request `%1` timed out after %2 ms.")
                                         .arg(method)
                                         .arg(timeoutMs);
        }

        return rpcResult;
    }

    static QString formatRpcError(const RpcResult &rpcResult)
    {
        QString message = rpcResult.errorMessage.trimmed();
        if (message.isEmpty())
        {
            message = QStringLiteral("Unknown MCP protocol error.");
        }

        if (!rpcResult.errorData.isUndefined() && !rpcResult.errorData.isNull())
        {
            const QString details = prettyJson(rpcResult.errorData);
            if (!details.isEmpty())
            {
                message += QStringLiteral("\n\nDetails:\n%1").arg(details);
            }
        }

        return message;
    }

    static QString flattenCallResult(const QJsonObject &resultObject)
    {
        const QString contentText =
            flattenToolContent(resultObject.value(QStringLiteral("content")).toArray());
        const QString structuredContent =
            prettyJson(resultObject.value(QStringLiteral("structuredContent")));

        QString combined = contentText;
        if (!structuredContent.isEmpty())
        {
            if (!combined.isEmpty())
            {
                combined += QStringLiteral("\n\n");
            }
            combined += structuredContent;
        }

        return combined.trimmed();
    }

    static QString recentLogsSuffix(const ServerSession &session)
    {
        if (session.recentLogLines.isEmpty())
        {
            return {};
        }

        return QStringLiteral("\n\nRecent server logs:\n%1")
            .arg(session.recentLogLines.join(QLatin1Char('\n')));
    }

    ToolRegistry *registry = nullptr;
    QMap<QString, std::shared_ptr<ServerSession>> sessions;
    QStringList registeredToolNames;
};

McpToolManager::McpToolManager(ToolRegistry *toolRegistry, QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(toolRegistry))
{
}

McpToolManager::~McpToolManager() = default;

QStringList McpToolManager::refreshForProject(const QString &projectDir)
{
    QStringList messages;

    m_impl->clearDynamicTools();

    qtmcp::ServerDefinitions mergedDefinitions = settings().mcpServers;
    const qtmcp::ServerDefinitions projectDefinitions =
        loadProjectServerDefinitions(projectDir, &messages);
    for (auto it = projectDefinitions.begin(); it != projectDefinitions.end(); ++it)
    {
        mergedDefinitions.insert(it.key(), it.value());
    }

    QSet<QString> usedExposedNames;
    int enabledServerCount = 0;
    int loadedToolCount = 0;

    for (auto defIt = mergedDefinitions.begin(); defIt != mergedDefinitions.end(); ++defIt)
    {
        const QString serverName = defIt.key();
        const qtmcp::ServerDefinition &definition = defIt.value();

        if (!definition.enabled)
        {
            continue;
        }
        ++enabledServerCount;

        if (definition.transport != QStringLiteral("stdio") &&
            definition.transport != QStringLiteral("http"))
        {
            messages.append(QStringLiteral("ℹ MCP: server `%1` uses unsupported runtime transport "
                                           "`%2`; supported transports are `stdio` and `http`.")
                                .arg(serverName, definition.transport));
            continue;
        }

        if (definition.transport == QStringLiteral("stdio") &&
            definition.command.trimmed().isEmpty())
        {
            messages.append(
                QStringLiteral("⚠ MCP: server `%1` is enabled but has no command configured.")
                    .arg(serverName));
            continue;
        }

        if (definition.transport == QStringLiteral("http") && definition.url.trimmed().isEmpty())
        {
            messages.append(
                QStringLiteral("⚠ MCP: server `%1` is enabled but has no URL configured.")
                    .arg(serverName));
            continue;
        }

        auto session = std::make_shared<ServerSession>();
        session->serverName = serverName;
        session->definition = definition;
        session->client = std::make_unique<qtmcp::Client>();

        const QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
        QString protocolVersion;
        if (definition.transport == QStringLiteral("stdio"))
        {
            qtmcp::StdioTransportConfig transportConfig;
            transportConfig.program = definition.command;
            transportConfig.arguments = definition.args;
            transportConfig.workingDirectory = projectDir;
            transportConfig.environment = processEnvironment;
            for (auto envIt = definition.env.begin(); envIt != definition.env.end(); ++envIt)
            {
                transportConfig.environment.insert(envIt.key(), envIt.value());
            }

            session->client->setTransport(
                std::make_unique<qtmcp::StdioTransport>(std::move(transportConfig)));
            protocolVersion = QString::fromLatin1(kMcpProtocolVersionStdio);
        }
        else
        {
            qtmcp::HttpTransportConfig transportConfig;
            transportConfig.endpoint = QUrl(expandEnvironmentPlaceholders(
                definition.url, serverName, QStringLiteral("the endpoint URL"), processEnvironment,
                &messages));
            transportConfig.protocolVersion = QString::fromLatin1(kMcpProtocolVersionHttp);
            transportConfig.requestTimeoutMs = definition.requestTimeoutMs;
            transportConfig.oauthEnabled =
                extraFieldBool(definition.extraFields, QStringLiteral("oauthEnabled"),
                               definition.headers.isEmpty());
            transportConfig.oauthClientId =
                extraFieldString(definition.extraFields, QStringLiteral("oauthClientId"));
            transportConfig.oauthClientSecret = expandEnvironmentPlaceholders(
                extraFieldString(definition.extraFields, QStringLiteral("oauthClientSecret")),
                serverName, QStringLiteral("oauthClientSecret"), processEnvironment, &messages);
            transportConfig.oauthClientName =
                extraFieldString(definition.extraFields, QStringLiteral("oauthClientName"));
            if (transportConfig.oauthClientName.isEmpty())
            {
                transportConfig.oauthClientName = QStringLiteral("qcai2");
            }
            transportConfig.oauthScopes =
                extraFieldStringList(definition.extraFields, QStringLiteral("oauthScopes"));
            for (auto headerIt = definition.headers.begin(); headerIt != definition.headers.end();
                 ++headerIt)
            {
                transportConfig.headers.insert(
                    headerIt.key(), expandEnvironmentPlaceholders(
                                        headerIt.value(), serverName,
                                        QStringLiteral("header `%1`").arg(headerIt.key()),
                                        processEnvironment, &messages));
            }

            session->client->setTransport(
                std::make_unique<qtmcp::HttpTransport>(std::move(transportConfig)));
            protocolVersion = QString::fromLatin1(kMcpProtocolVersionHttp);
        }
        QObject::connect(session->client.get(), &qtmcp::Client::transportLogMessage, this,
                         [serverName, sessionPtr = session.get()](const QString &message) {
                             const QString trimmed = message.trimmed();
                             if (!trimmed.isEmpty())
                             {
                                 sessionPtr->recentLogLines.append(trimmed);
                                 while (sessionPtr->recentLogLines.size() > 30)
                                 {
                                     sessionPtr->recentLogLines.removeFirst();
                                 }
                             }
                             QCAI_DEBUG("MCP", QStringLiteral("[%1] %2").arg(serverName, message));
                         });

        QString startupError;
        if (!Impl::waitForConnection(session->client.get(), definition.startupTimeoutMs,
                                     &startupError))
        {
            messages.append(
                QStringLiteral("⚠ MCP: failed to start `%1`: %2").arg(serverName, startupError));
            continue;
        }

        const QJsonObject initializeParams{
            {QStringLiteral("protocolVersion"), protocolVersion},
            {QStringLiteral("capabilities"), QJsonObject{}},
            {QStringLiteral("clientInfo"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("qcai2")},
                         {QStringLiteral("version"), Migration::currentRevisionString()}}}};
        const RpcResult initializeResult =
            Impl::sendRequestSync(session->client.get(), QStringLiteral("initialize"),
                                  initializeParams, definition.requestTimeoutMs);
        if (initializeResult.protocolError)
        {
            messages.append(QStringLiteral("⚠ MCP: initialize failed for `%1`: %2")
                                .arg(serverName, Impl::formatRpcError(initializeResult) +
                                                     Impl::recentLogsSuffix(*session)));
            session->client->stop();
            continue;
        }
        if (!initializeResult.result.isObject())
        {
            messages.append(
                QStringLiteral("⚠ MCP: initialize for `%1` returned a non-object result.")
                    .arg(serverName));
            session->client->stop();
            continue;
        }

        const QJsonObject initializeObject = initializeResult.result.toObject();
        session->negotiatedProtocolVersion =
            initializeObject.value(QStringLiteral("protocolVersion")).toString();
        session->capabilities = initializeObject.value(QStringLiteral("capabilities")).toObject();
        if (!session->capabilities.contains(QStringLiteral("tools")))
        {
            messages.append(QStringLiteral("ℹ MCP: server `%1` does not advertise tool support.")
                                .arg(serverName));
            session->client->stop();
            continue;
        }

        session->client->sendNotification(QStringLiteral("notifications/initialized"));

        QJsonArray collectedTools;
        QString nextCursor;
        int pageCount = 0;
        while (pageCount < kMaxToolsListPages)
        {
            ++pageCount;
            QJsonObject listParams;
            if (!nextCursor.isEmpty())
            {
                listParams.insert(QStringLiteral("cursor"), nextCursor);
            }

            const RpcResult listResult =
                Impl::sendRequestSync(session->client.get(), QStringLiteral("tools/list"),
                                      listParams.isEmpty() ? QJsonValue() : QJsonValue(listParams),
                                      definition.requestTimeoutMs);
            if (listResult.protocolError)
            {
                messages.append(QStringLiteral("⚠ MCP: tools/list failed for `%1`: %2")
                                    .arg(serverName, Impl::formatRpcError(listResult) +
                                                         Impl::recentLogsSuffix(*session)));
                collectedTools = {};
                break;
            }
            if (!listResult.result.isObject())
            {
                messages.append(
                    QStringLiteral("⚠ MCP: tools/list for `%1` returned a non-object result.")
                        .arg(serverName));
                collectedTools = {};
                break;
            }

            const QJsonObject listObject = listResult.result.toObject();
            const QJsonValue toolsValue = listObject.value(QStringLiteral("tools"));
            if (!toolsValue.isArray())
            {
                messages.append(
                    QStringLiteral("⚠ MCP: tools/list for `%1` returned no `tools` array.")
                        .arg(serverName));
                collectedTools = {};
                break;
            }

            const QJsonArray toolsPage = toolsValue.toArray();
            for (const QJsonValue &toolValue : toolsPage)
            {
                collectedTools.append(toolValue);
            }

            nextCursor = listObject.value(QStringLiteral("nextCursor")).toString();
            if (nextCursor.isEmpty())
            {
                break;
            }
        }

        if (collectedTools.isEmpty())
        {
            if (pageCount >= kMaxToolsListPages && !nextCursor.isEmpty())
            {
                messages.append(
                    QStringLiteral("⚠ MCP: tool pagination for `%1` exceeded %2 pages.")
                        .arg(serverName)
                        .arg(kMaxToolsListPages));
            }
            session->client->stop();
            continue;
        }

        int registeredForServer = 0;
        for (const QJsonValue &toolValue : collectedTools)
        {
            if (!toolValue.isObject())
            {
                continue;
            }

            const QJsonObject toolObject = toolValue.toObject();
            const QString toolName = toolObject.value(QStringLiteral("name")).toString().trimmed();
            if (toolName.isEmpty())
            {
                continue;
            }

            RemoteTool remoteTool;
            remoteTool.serverName = serverName;
            remoteTool.toolName = toolName;
            remoteTool.description = toolObject.value(QStringLiteral("description")).toString();
            remoteTool.inputSchema = toolObject.value(QStringLiteral("inputSchema")).toObject();
            remoteTool.exposedName = uniqueExposedToolName(serverName, toolName, usedExposedNames);
            usedExposedNames.insert(remoteTool.exposedName);

            session->toolsByExposedName.insert(remoteTool.exposedName, remoteTool);
            if (m_impl->registry != nullptr)
            {
                m_impl->registry->registerTool(std::make_shared<McpDynamicTool>(this, remoteTool));
                m_impl->registeredToolNames.append(remoteTool.exposedName);
            }
            ++registeredForServer;
        }

        if (registeredForServer == 0)
        {
            messages.append(
                QStringLiteral("ℹ MCP: server `%1` started but exposed no usable tools.")
                    .arg(serverName));
            session->client->stop();
            continue;
        }

        messages.append(QStringLiteral("🔌 MCP: server `%1` ready with %2 tool(s).")
                            .arg(serverName)
                            .arg(registeredForServer));
        loadedToolCount += registeredForServer;
        m_impl->sessions.insert(serverName, session);
    }

    if (enabledServerCount > 0 && loadedToolCount == 0)
    {
        messages.append(QStringLiteral("⚠ MCP: %1 enabled server(s) were configured, but no MCP "
                                       "tools were loaded for this run.")
                            .arg(enabledServerCount));
    }

    return messages;
}

QString McpToolManager::executeTool(const QString &exposedToolName, const QJsonObject &args) const
{
    for (auto sessionIt = m_impl->sessions.begin(); sessionIt != m_impl->sessions.end();
         ++sessionIt)
    {
        const auto toolIt = sessionIt.value()->toolsByExposedName.find(exposedToolName);
        if (toolIt == sessionIt.value()->toolsByExposedName.end())
        {
            continue;
        }

        const RemoteTool &remoteTool = toolIt.value();
        const RpcResult callResult =
            Impl::sendRequestSync(sessionIt.value()->client.get(), QStringLiteral("tools/call"),
                                  QJsonObject{{QStringLiteral("name"), remoteTool.toolName},
                                              {QStringLiteral("arguments"), args}},
                                  sessionIt.value()->definition.requestTimeoutMs);

        if (callResult.protocolError)
        {
            return QStringLiteral("Error: MCP tool `%1` on server `%2` failed: %3")
                .arg(remoteTool.toolName, remoteTool.serverName, Impl::formatRpcError(callResult));
        }

        if (!callResult.result.isObject())
        {
            return QStringLiteral(
                       "Error: MCP tool `%1` on server `%2` returned a non-object result.")
                .arg(remoteTool.toolName, remoteTool.serverName);
        }

        const QJsonObject resultObject = callResult.result.toObject();
        const QString flattened = Impl::flattenCallResult(resultObject);
        const bool isError = resultObject.value(QStringLiteral("isError")).toBool(false);

        if (isError)
        {
            const QString message =
                flattened.isEmpty() ? QStringLiteral("MCP tool reported an error without content.")
                                    : flattened;
            return QStringLiteral("Error: %1").arg(message);
        }

        if (!flattened.isEmpty())
        {
            return flattened;
        }

        return QString::fromUtf8(QJsonDocument(resultObject).toJson(QJsonDocument::Indented))
            .trimmed();
    }

    return QStringLiteral("Error: unknown MCP tool '%1'.").arg(exposedToolName);
}

}  // namespace qcai2
