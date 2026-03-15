/*! Implements the streamable HTTP transport for the Qt MCP client. */

#include <qtmcp/HttpTransport.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslError>
#include <QStandardPaths>
#include <QTimer>

#if QTMCP_HAS_NETWORKAUTH
#include <QDesktopServices>
#include <QHostAddress>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QSet>
#endif

#include <algorithm>
#include <array>
#include <utility>

namespace qtmcp
{

namespace
{

constexpr int kMaxLoggedPayloadChars = 2000;

QString formatPayloadForLog(const QByteArray &payload)
{
    QString text = QString::fromUtf8(payload);
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    if (text.size() > kMaxLoggedPayloadChars)
    {
        text = text.left(kMaxLoggedPayloadChars);
        text += QStringLiteral("... [truncated]");
    }
    return text;
}

QString normalizedContentType(QNetworkReply *reply)
{
    const QString raw = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const qsizetype separator = raw.indexOf(QLatin1Char(';'));
    return (separator >= 0 ? raw.left(separator) : raw).trimmed().toLower();
}

QString scopesCacheKey(QStringList scopes)
{
    std::sort(scopes.begin(), scopes.end());
    return scopes.join(QLatin1Char(' '));
}

QString oauthCacheKey(const HttpTransportConfig &config)
{
    return QStringLiteral("%1|%2|%3")
        .arg(config.endpoint.toString(QUrl::FullyEncoded), config.oauthClientId,
             scopesCacheKey(config.oauthScopes));
}

QString tokenPresenceSummary(const QString &token)
{
    return token.isEmpty() ? QStringLiteral("absent")
                           : QStringLiteral("present(len=%1)").arg(token.size());
}

QString expirySummary(const QDateTime &expiration)
{
    return expiration.isValid() ? expiration.toString(Qt::ISODate) : QStringLiteral("none");
}

struct OAuthCacheEntry
{
    QString accessToken;
    QString refreshToken;
    QDateTime expiration;
    QUrl authorizationUrl;
    QUrl tokenUrl;
    QString clientId;
    QString clientSecret;
    QString resource;
    QUrl resourceMetadataUrl;
};

QHash<QString, OAuthCacheEntry> &oauthCache()
{
    static QHash<QString, OAuthCacheEntry> cache;
    return cache;
}

QString persistentOAuthCachePath()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (((basePath.trimmed().isEmpty()) == true))
    {
        basePath = QDir::homePath() + QStringLiteral("/.config");
    }
    return QDir(basePath).filePath(QStringLiteral("qtmcp/oauth-cache.json"));
}

QJsonObject oauthCacheEntryToJson(const OAuthCacheEntry &entry)
{
    QJsonObject object;
    if (((!entry.accessToken.isEmpty()) == true))
    {
        object.insert(QStringLiteral("accessToken"), entry.accessToken);
    }
    if (((!entry.refreshToken.isEmpty()) == true))
    {
        object.insert(QStringLiteral("refreshToken"), entry.refreshToken);
    }
    if (entry.expiration.isValid() == true)
    {
        object.insert(QStringLiteral("expiration"), entry.expiration.toString(Qt::ISODate));
    }
    if (((entry.authorizationUrl.isValid() && !entry.authorizationUrl.isEmpty()) == true))
    {
        object.insert(QStringLiteral("authorizationUrl"), entry.authorizationUrl.toString());
    }
    if (((entry.tokenUrl.isValid() && !entry.tokenUrl.isEmpty()) == true))
    {
        object.insert(QStringLiteral("tokenUrl"), entry.tokenUrl.toString());
    }
    if (((!entry.clientId.isEmpty()) == true))
    {
        object.insert(QStringLiteral("clientId"), entry.clientId);
    }
    if (((!entry.clientSecret.isEmpty()) == true))
    {
        object.insert(QStringLiteral("clientSecret"), entry.clientSecret);
    }
    if (((!entry.resource.isEmpty()) == true))
    {
        object.insert(QStringLiteral("resource"), entry.resource);
    }
    if (((entry.resourceMetadataUrl.isValid() && !entry.resourceMetadataUrl.isEmpty()) == true))
    {
        object.insert(QStringLiteral("resourceMetadataUrl"), entry.resourceMetadataUrl.toString());
    }
    return object;
}

bool oauthCacheEntryFromJson(const QJsonObject &object, OAuthCacheEntry *entry)
{
    if (((entry == nullptr) == true))
    {
        return false;
    }

    OAuthCacheEntry parsed;
    parsed.accessToken = object.value(QStringLiteral("accessToken")).toString();
    parsed.refreshToken = object.value(QStringLiteral("refreshToken")).toString();
    parsed.expiration =
        QDateTime::fromString(object.value(QStringLiteral("expiration")).toString(), Qt::ISODate);
    parsed.authorizationUrl = QUrl(object.value(QStringLiteral("authorizationUrl")).toString());
    parsed.tokenUrl = QUrl(object.value(QStringLiteral("tokenUrl")).toString());
    parsed.clientId = object.value(QStringLiteral("clientId")).toString();
    parsed.clientSecret = object.value(QStringLiteral("clientSecret")).toString();
    parsed.resource = object.value(QStringLiteral("resource")).toString();
    parsed.resourceMetadataUrl =
        QUrl(object.value(QStringLiteral("resourceMetadataUrl")).toString());
    *entry = parsed;
    return true;
}

bool loadPersistentOAuthCacheEntry(const QString &cacheKey, OAuthCacheEntry *entry,
                                   QString *error = nullptr)
{
    QFile file(persistentOAuthCachePath());
    if (file.exists() == false)
    {
        return false;
    }
    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open OAuth cache file for reading: %1")
                         .arg(file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (((parseError.error != QJsonParseError::NoError || !document.isObject()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to parse OAuth cache file: %1")
                         .arg(parseError.error == QJsonParseError::NoError
                                  ? QStringLiteral("root must be an object")
                                  : parseError.errorString());
        }
        return false;
    }

    const QJsonValue value = document.object().value(cacheKey);
    if (value.isObject() == false)
    {
        return false;
    }
    return oauthCacheEntryFromJson(value.toObject(), entry);
}

bool savePersistentOAuthCacheEntry(const QString &cacheKey, const OAuthCacheEntry &entry,
                                   QString *error = nullptr)
{
    const QString path = persistentOAuthCachePath();
    QJsonObject root;

    QFile existingFile(path);
    if (existingFile.exists() == true)
    {
        if (existingFile.open(QIODevice::ReadOnly) == false)
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("Failed to open OAuth cache file for reading: %1")
                             .arg(existingFile.errorString());
            }
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(existingFile.readAll(), &parseError);
        if (((parseError.error != QJsonParseError::NoError || !document.isObject()) == true))
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("Failed to parse OAuth cache file: %1")
                             .arg(parseError.error == QJsonParseError::NoError
                                      ? QStringLiteral("root must be an object")
                                      : parseError.errorString());
            }
            return false;
        }
        root = document.object();
    }

    root.insert(cacheKey, oauthCacheEntryToJson(entry));

    const QFileInfo fileInfo(path);
    if (((!QDir().mkpath(fileInfo.absolutePath())) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to create OAuth cache directory.");
        }
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to open OAuth cache file for writing: %1")
                         .arg(file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to commit OAuth cache file.");
        }
        return false;
    }

    return true;
}

QMap<QString, QString> parseBearerChallengeParameters(const QByteArray &wwwAuthenticateHeader)
{
    QMap<QString, QString> parameters;
    const QString header = QString::fromUtf8(wwwAuthenticateHeader);
    const qsizetype bearerIndex = header.indexOf(QRegularExpression(
        QStringLiteral("(^|\\s|,)Bearer\\s+"), QRegularExpression::CaseInsensitiveOption));
    if (bearerIndex < 0)
    {
        return parameters;
    }

    const qsizetype paramsStart = header.indexOf(QLatin1Char(' '), bearerIndex);
    if (paramsStart < 0)
    {
        return parameters;
    }

    const QString params = header.mid(paramsStart + 1);
    static const QRegularExpression pairExpression(
        QStringLiteral(R"re(([A-Za-z_][A-Za-z0-9_-]*)\s*=\s*"([^"]*)")re"));
    auto it = pairExpression.globalMatch(params);
    while (it.hasNext())
    {
        const auto match = it.next();
        parameters.insert(match.captured(1), match.captured(2));
    }

    return parameters;
}

#if QTMCP_HAS_NETWORKAUTH

constexpr int kOAuthInteractiveTimeoutMs = 5 * 60 * 1000;

QUrl authorizationBaseUrl(const QUrl &endpoint)
{
    QUrl base = endpoint.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    base.setPath(QString());
    return base;
}

QUrl appendPath(const QUrl &base, const QString &path)
{
    QUrl url(base);
    url.setPath(path);
    return url;
}

struct SyncHttpResponse
{
    int statusCode = 0;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    QString errorString;
    QByteArray body;
    QJsonDocument jsonBody;
    bool hasJsonBody = false;
};

SyncHttpResponse performSyncRequest(QNetworkAccessManager *networkAccessManager,
                                    QNetworkRequest request, const QByteArray &verb,
                                    const QByteArray &body = {})
{
    SyncHttpResponse result;
    QNetworkReply *reply = nullptr;

    if (((verb == QByteArrayLiteral("GET")) == true))
    {
        reply = networkAccessManager->get(request);
    }
    else if (((verb == QByteArrayLiteral("POST")) == true))
    {
        reply = networkAccessManager->post(request, body);
    }
    else
    {
        return result;
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.networkError = reply->error();
    result.errorString = reply->errorString();
    result.body = reply->readAll();

    if (((!result.body.isEmpty()) == true))
    {
        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(result.body, &parseError);
        if (((parseError.error == QJsonParseError::NoError) == true))
        {
            result.jsonBody = json;
            result.hasJsonBody = true;
        }
    }

    reply->deleteLater();
    return result;
}

struct OAuthEndpoints
{
    QUrl authorizationEndpoint;
    QUrl tokenEndpoint;
    QUrl registrationEndpoint;
};

struct ProtectedResourceMetadata
{
    QString resource;
    QList<QUrl> authorizationServers;
    QUrl metadataUrl;
};

struct RegisteredOAuthClient
{
    QString clientId;
    QString clientSecret;
};

struct OAuthListenAttempt
{
    QString callbackHost;
    QHostAddress address;
    QString label;
};

QSet<QByteArray> requestedScopeTokens(const QStringList &scopes)
{
    QSet<QByteArray> tokens;
    for (const QString &scope : scopes)
    {
        const QString trimmed = scope.trimmed();
        if (!trimmed.isEmpty())
        {
            tokens.insert(trimmed.toUtf8());
        }
    }
    return tokens;
}

QUrl oauthAuthorizationMetadataUrl(const QUrl &authorizationServer)
{
    QUrl base(authorizationServer);
    base.setQuery(QString());
    base.setFragment(QString());
    return appendPath(base, QStringLiteral("/.well-known/oauth-authorization-server"));
}

QUrl defaultProtectedResourceMetadataUrl(const QUrl &endpoint)
{
    return appendPath(authorizationBaseUrl(endpoint),
                      QStringLiteral("/.well-known/oauth-protected-resource"));
}

bool loadProtectedResourceMetadata(QNetworkAccessManager *networkAccessManager,
                                   const HttpTransportConfig &config, const QUrl &metadataUrl,
                                   ProtectedResourceMetadata *metadata, QString *logMessage)
{
    if (((!metadataUrl.isValid() || metadataUrl.isEmpty()) == true))
    {
        return false;
    }

    QNetworkRequest request(metadataUrl);
    request.setRawHeader("Accept", "application/json");
    if (((config.requestTimeoutMs > 0) == true))
    {
        request.setTransferTimeout(config.requestTimeoutMs);
    }

    const SyncHttpResponse response =
        performSyncRequest(networkAccessManager, request, QByteArrayLiteral("GET"));
    if (response.networkError != QNetworkReply::NoError || response.statusCode < 200 ||
        response.statusCode >= 300 || !response.hasJsonBody || !response.jsonBody.isObject())
    {
        return false;
    }

    const QJsonObject root = response.jsonBody.object();
    metadata->metadataUrl = metadataUrl;
    metadata->resource = root.value(QStringLiteral("resource")).toString();
    const QJsonArray authorizationServers =
        root.value(QStringLiteral("authorization_servers")).toArray();
    for (const QJsonValue &value : authorizationServers)
    {
        const QUrl url(value.toString());
        if (url.isValid() && !url.isEmpty())
        {
            metadata->authorizationServers.append(url);
        }
    }

    if (((logMessage != nullptr) == true))
    {
        *logMessage = QStringLiteral("Loaded OAuth protected-resource metadata from %1")
                          .arg(metadataUrl.toString());
    }
    return true;
}

bool discoverOAuthEndpoints(QNetworkAccessManager *networkAccessManager,
                            const HttpTransportConfig &config, const QUrl &metadataUrl,
                            OAuthEndpoints *endpoints, QString *logMessage)
{
    const QUrl resolvedMetadataUrl =
        metadataUrl.isValid() && !metadataUrl.isEmpty()
            ? metadataUrl
            : oauthAuthorizationMetadataUrl(authorizationBaseUrl(config.endpoint));

    QNetworkRequest request(resolvedMetadataUrl);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("MCP-Protocol-Version", config.protocolVersion.toUtf8());
    if (((config.requestTimeoutMs > 0) == true))
    {
        request.setTransferTimeout(config.requestTimeoutMs);
    }

    const SyncHttpResponse response =
        performSyncRequest(networkAccessManager, request, QByteArrayLiteral("GET"));

    if (response.networkError == QNetworkReply::NoError && response.statusCode >= 200 &&
        response.statusCode < 300 && response.hasJsonBody && response.jsonBody.isObject())
    {
        const QJsonObject root = response.jsonBody.object();
        endpoints->authorizationEndpoint =
            QUrl(root.value(QStringLiteral("authorization_endpoint")).toString());
        endpoints->tokenEndpoint = QUrl(root.value(QStringLiteral("token_endpoint")).toString());
        endpoints->registrationEndpoint =
            QUrl(root.value(QStringLiteral("registration_endpoint")).toString());

        if (((logMessage != nullptr) == true))
        {
            *logMessage = QStringLiteral("Discovered OAuth metadata at %1")
                              .arg(resolvedMetadataUrl.toString());
        }
        return endpoints->authorizationEndpoint.isValid() && endpoints->tokenEndpoint.isValid();
    }

    const QUrl authBase = authorizationBaseUrl(config.endpoint);
    endpoints->authorizationEndpoint = appendPath(authBase, QStringLiteral("/authorize"));
    endpoints->tokenEndpoint = appendPath(authBase, QStringLiteral("/token"));
    endpoints->registrationEndpoint = QUrl();
    if (((logMessage != nullptr) == true))
    {
        *logMessage =
            QStringLiteral("OAuth metadata unavailable at %1, using default authorization/token "
                           "endpoint paths without assuming dynamic client registration.")
                .arg(resolvedMetadataUrl.toString());
    }
    return true;
}

bool registerOAuthClient(QNetworkAccessManager *networkAccessManager,
                         const HttpTransportConfig &config, const QUrl &registrationEndpoint,
                         const QString &callbackUrl, RegisteredOAuthClient *client, QString *error)
{
    QNetworkRequest request(registrationEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (((config.requestTimeoutMs > 0) == true))
    {
        request.setTransferTimeout(config.requestTimeoutMs);
    }

    const QJsonObject body{
        {QStringLiteral("client_name"), config.oauthClientName},
        {QStringLiteral("redirect_uris"), QJsonArray{callbackUrl}},
        {QStringLiteral("grant_types"),
         QJsonArray{QStringLiteral("authorization_code"), QStringLiteral("refresh_token")}},
        {QStringLiteral("response_types"), QJsonArray{QStringLiteral("code")}},
        {QStringLiteral("token_endpoint_auth_method"),
         config.oauthClientSecret.isEmpty() ? QStringLiteral("none")
                                            : QStringLiteral("client_secret_post")}};

    const SyncHttpResponse response =
        performSyncRequest(networkAccessManager, request, QByteArrayLiteral("POST"),
                           QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (response.networkError != QNetworkReply::NoError || response.statusCode < 200 ||
        response.statusCode >= 300 || !response.hasJsonBody || !response.jsonBody.isObject())
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Dynamic client registration failed at %1 (HTTP %2): %3")
                         .arg(registrationEndpoint.toString())
                         .arg(response.statusCode)
                         .arg(response.errorString);
        }
        return false;
    }

    const QJsonObject root = response.jsonBody.object();
    client->clientId = root.value(QStringLiteral("client_id")).toString();
    client->clientSecret = root.value(QStringLiteral("client_secret")).toString();
    if (client->clientId.isEmpty())
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Dynamic client registration returned no client_id.");
        }
        return false;
    }

    return true;
}

bool startOAuthCallbackListener(QOAuthHttpServerReplyHandler *replyHandler, QString *details)
{
    if (((replyHandler == nullptr) == true))
    {
        return false;
    }

    const std::array<OAuthListenAttempt, 4> attempts{{
        {QStringLiteral("127.0.0.1"), QHostAddress(QStringLiteral("127.0.0.1")),
         QStringLiteral("127.0.0.1")},
        {QStringLiteral("localhost"), QHostAddress(QHostAddress::LocalHost),
         QStringLiteral("localhost / LocalHost")},
        {QStringLiteral("::1"), QHostAddress(QHostAddress::LocalHostIPv6), QStringLiteral("::1")},
        {QStringLiteral("127.0.0.1"), QHostAddress(QHostAddress::AnyIPv4),
         QStringLiteral("AnyIPv4 via 127.0.0.1")},
    }};

    QStringList failureLabels;
    for (const OAuthListenAttempt &attempt : attempts)
    {
        replyHandler->close();
        replyHandler->setCallbackHost(attempt.callbackHost);
        if (replyHandler->listen(attempt.address, 0))
        {
            if (details != nullptr)
            {
                *details = QStringLiteral("Listening for OAuth callback on %1")
                               .arg(replyHandler->callback());
            }
            return true;
        }
        failureLabels.append(attempt.label);
    }

    if (((details != nullptr) == true))
    {
        *details = QStringLiteral("Failed to start local OAuth callback listener. Tried %1.")
                       .arg(failureLabels.join(QStringLiteral(", ")));
    }
    return false;
}

#endif

}  // namespace

struct HttpTransport::ReplyState
{
    QByteArray buffer;
    QByteArray currentEventData;
    QString currentEventType;
    QByteArray requestPayload;
    bool isSse = false;
    bool oauthRetried = false;
    int emittedMessages = 0;
};

HttpTransport::HttpTransport(HttpTransportConfig config, QObject *parent)
    : Transport(parent), m_config(std::move(config)),
      m_networkAccessManager(new QNetworkAccessManager(this))
{
}

HttpTransport::~HttpTransport() = default;

QString HttpTransport::transportName() const
{
    return QStringLiteral("http");
}

Transport::State HttpTransport::state() const
{
    return m_state;
}

const HttpTransportConfig &HttpTransport::config() const
{
    return m_config;
}

QString HttpTransport::lastOAuthError() const
{
    return m_lastOAuthError;
}

bool HttpTransport::lastAuthorizationRequired() const
{
    return m_lastAuthorizationRequired;
}

bool HttpTransport::hasCachedOAuthCredentials() const
{
    return !m_accessToken.isEmpty() || !m_refreshToken.isEmpty();
}

bool HttpTransport::authorize(QString *errorMessage)
{
    emit logMessage(
        QStringLiteral(
            "Manual OAuth authorization requested: accessToken=%1 refreshToken=%2 expires=%3")
            .arg(tokenPresenceSummary(m_accessToken), tokenPresenceSummary(m_refreshToken),
                 expirySummary(m_accessTokenExpiration)));
    m_lastAuthorizationRequired = false;
    if (((m_state == State::Disconnected) == true))
    {
        start();
    }

    if (((m_state != State::Connected) == true))
    {
        if (((errorMessage != nullptr) == true))
        {
            *errorMessage = m_lastOAuthError.isEmpty()
                                ? QStringLiteral("MCP HTTP transport is not connected.")
                                : m_lastOAuthError;
        }
        return false;
    }

    const bool authorized = ensureAuthorized();
    if (((!authorized && m_lastOAuthError.isEmpty()) == true))
    {
        m_lastOAuthError = QStringLiteral("OAuth authorization did not complete successfully.");
    }
    if (((errorMessage != nullptr) == true))
    {
        *errorMessage = authorized ? QString()
                                   : (m_lastOAuthError.isEmpty()
                                          ? QStringLiteral("MCP OAuth authorization failed.")
                                          : m_lastOAuthError);
    }
    return authorized;
}

void HttpTransport::start()
{
    if (((m_state != State::Disconnected) == true))
    {
        return;
    }

    if (((!m_config.endpoint.isValid() || m_config.endpoint.isEmpty()) == true))
    {
        m_lastOAuthError = QStringLiteral("MCP HTTP transport requires a valid endpoint URL.");
        emit errorOccurred(m_lastOAuthError);
        return;
    }

    const QString scheme = m_config.endpoint.scheme().toLower();
    if (((scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) == true))
    {
        m_lastOAuthError = QStringLiteral("MCP HTTP transport only supports http/https URLs.");
        emit errorOccurred(m_lastOAuthError);
        return;
    }

    loadCachedOAuthState();
    m_lastOAuthError.clear();
    m_lastAuthorizationRequired = false;
    emit logMessage(QStringLiteral("HTTP transport ready: endpoint=%1 oauth=%2")
                        .arg(m_config.endpoint.toString(), m_config.oauthEnabled
                                                               ? QStringLiteral("enabled")
                                                               : QStringLiteral("disabled")));
    if (m_config.oauthEnabled == true)
    {
        emit logMessage(QStringLiteral("Initial OAuth state: accessToken=%1 refreshToken=%2 "
                                       "expires=%3 clientIdConfigured=%4")
                            .arg(tokenPresenceSummary(m_accessToken),
                                 tokenPresenceSummary(m_refreshToken),
                                 expirySummary(m_accessTokenExpiration),
                                 m_registeredClientId.isEmpty() ? QStringLiteral("no")
                                                                : QStringLiteral("yes")));
    }
    setState(State::Connected);
    emit started();
}

void HttpTransport::stop()
{
    if (((m_state == State::Disconnected || m_state == State::Stopping) == true))
    {
        return;
    }

    setState(State::Stopping);
    emit logMessage(
        QStringLiteral("Stopping HTTP transport: %1 active request(s)").arg(m_replyStates.size()));

    const auto replies = m_replyStates.keys();
    for (QNetworkReply *reply : replies)
    {
        reply->abort();
    }
    m_replyStates.clear();
    m_sessionId.clear();
    m_lastAuthorizationRequired = false;

    setState(State::Disconnected);
    emit stopped();
}

bool HttpTransport::sendMessage(const QJsonObject &message)
{
    if (((m_state != State::Connected) == true))
    {
        emit errorOccurred(QStringLiteral(
            "Cannot send an MCP HTTP message while the transport is disconnected."));
        return false;
    }

    m_lastAuthorizationRequired = false;
    if (m_config.oauthEnabled == true)
    {
        emit logMessage(
            QStringLiteral("Preparing HTTP MCP request: accessToken=%1 refreshToken=%2 expires=%3")
                .arg(tokenPresenceSummary(m_accessToken), tokenPresenceSummary(m_refreshToken),
                     expirySummary(m_accessTokenExpiration)));
    }

    if (((m_config.oauthEnabled && !m_refreshToken.isEmpty() &&
          m_accessTokenExpiration.isValid() &&
          QDateTime::currentDateTimeUtc() >= m_accessTokenExpiration.addSecs(-30)) == true))
    {
        emit logMessage(QStringLiteral(
            "OAuth access token is near expiry; attempting refresh before request."));
        refreshAccessToken();
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return postPayload(payload, false);
}

QNetworkRequest HttpTransport::buildPostRequest() const
{
    QNetworkRequest request(m_config.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("MCP-Protocol-Version", m_config.protocolVersion.toUtf8());
    if (((m_config.requestTimeoutMs > 0) == true))
    {
        request.setTransferTimeout(m_config.requestTimeoutMs);
    }

    applyConfiguredHeaders(request);
    applySessionHeader(request);
    applyAuthorizationHeader(request);
    return request;
}

void HttpTransport::applyConfiguredHeaders(QNetworkRequest &request) const
{
    for (auto it = m_config.headers.begin(); ((it != m_config.headers.end()) == true); ++it)
    {
        if (((m_config.oauthEnabled && it.key().compare(QStringLiteral("Authorization"),
                                                        Qt::CaseInsensitive) == 0) == true))
        {
            continue;
        }
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
}

void HttpTransport::applySessionHeader(QNetworkRequest &request) const
{
    if (((!m_sessionId.isEmpty()) == true))
    {
        request.setRawHeader("Mcp-Session-Id", m_sessionId.toUtf8());
    }
}

void HttpTransport::applyAuthorizationHeader(QNetworkRequest &request) const
{
    if (((m_config.oauthEnabled && !m_accessToken.isEmpty()) == true))
    {
        emit const_cast<HttpTransport *>(this)->logMessage(
            QStringLiteral("Applying OAuth Authorization header: token=%1 expires=%2")
                .arg(tokenPresenceSummary(m_accessToken), expirySummary(m_accessTokenExpiration)));
        request.setRawHeader("Authorization",
                             QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    }
    else if (m_config.oauthEnabled == true)
    {
        emit const_cast<HttpTransport *>(this)->logMessage(
            QStringLiteral("Skipping OAuth Authorization header: accessToken=%1 expires=%2")
                .arg(tokenPresenceSummary(m_accessToken), expirySummary(m_accessTokenExpiration)));
    }
}

bool HttpTransport::postPayload(const QByteArray &payload, bool oauthRetried)
{
    const QNetworkRequest request = buildPostRequest();
    if (m_config.oauthEnabled == true)
    {
        emit logMessage(
            QStringLiteral(
                "Posting HTTP payload: oauthRetry=%1 accessToken=%2 refreshToken=%3 sessionId=%4")
                .arg(oauthRetried ? QStringLiteral("yes") : QStringLiteral("no"),
                     tokenPresenceSummary(m_accessToken), tokenPresenceSummary(m_refreshToken),
                     m_sessionId.isEmpty() ? QStringLiteral("absent")
                                           : QStringLiteral("present")));
    }
    emit logMessage(QStringLiteral("http -> %1").arg(formatPayloadForLog(payload)));

    QNetworkReply *reply = m_networkAccessManager->post(request, payload);
    attachReply(reply, payload, oauthRetried);
    return true;
}

void HttpTransport::setState(State state)
{
    if (((m_state == state) == true))
    {
        return;
    }

    m_state = state;
    QString stateLabel = QStringLiteral("unknown");
    switch (m_state)
    {
        case State::Disconnected:
            stateLabel = QStringLiteral("disconnected");
            break;
        case State::Starting:
            stateLabel = QStringLiteral("starting");
            break;
        case State::Connected:
            stateLabel = QStringLiteral("connected");
            break;
        case State::Stopping:
            stateLabel = QStringLiteral("stopping");
            break;
    }
    emit logMessage(QStringLiteral("http state changed: %1").arg(stateLabel));
    emit stateChanged(m_state);
}

void HttpTransport::attachReply(QNetworkReply *reply, const QByteArray &payload, bool oauthRetried)
{
    ReplyState state;
    state.requestPayload = payload;
    state.oauthRetried = oauthRetried;
    m_replyStates.insert(reply, state);

    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply]() { processReplyReadyRead(reply); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { processReplyFinished(reply); });
    connect(reply, &QNetworkReply::errorOccurred, this,
            [this, reply](QNetworkReply::NetworkError code) {
                emit logMessage(QStringLiteral("HTTP reply error: code=%1 text=%2")
                                    .arg(static_cast<int>(code))
                                    .arg(reply->errorString()));
            });
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList parts;
        for (const auto &error : errors)
        {
            parts.append(error.errorString());
        }
        if (((!parts.isEmpty()) == true))
        {
            emit logMessage(
                QStringLiteral("HTTP SSL errors: %1").arg(parts.join(QStringLiteral("; "))));
        }
    });
}

void HttpTransport::processReplyReadyRead(QNetworkReply *reply)
{
    auto it = m_replyStates.find(reply);
    if (((it == m_replyStates.end()) == true))
    {
        return;
    }

    applySessionId(reply);
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty() == true)
    {
        return;
    }

    it->buffer += chunk;
    emit logMessage(QStringLiteral("http reply bytes: %1").arg(chunk.size()));

    const QString contentType = normalizedContentType(reply);
    if (((contentType == QStringLiteral("text/event-stream")) == true))
    {
        it->isSse = true;
        processSseBuffer(reply, false);
    }
}

void HttpTransport::processReplyFinished(QNetworkReply *reply)
{
    auto it = m_replyStates.find(reply);
    if (((it == m_replyStates.end()) == true))
    {
        reply->deleteLater();
        return;
    }

    applySessionId(reply);

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = normalizedContentType(reply);
    emit logMessage(QStringLiteral("HTTP reply finished: status=%1 type=%2")
                        .arg(statusCode)
                        .arg(contentType.isEmpty() ? QStringLiteral("(none)") : contentType));

    if (((statusCode == 401 && it->emittedMessages == 0 && m_config.oauthEnabled &&
          !it->oauthRetried) == true))
    {
        m_lastAuthorizationRequired = true;
        const QByteArray retryPayload = it->requestPayload;
        if (((!it->buffer.isEmpty()) == true))
        {
            emit logMessage(QStringLiteral("http <- %1").arg(formatPayloadForLog(it->buffer)));
        }
        const QMap<QString, QString> challenge =
            parseBearerChallengeParameters(reply->rawHeader("WWW-Authenticate"));
        if (((!challenge.isEmpty()) == true))
        {
            const QString resourceMetadata =
                challenge.value(QStringLiteral("resource_metadata")).trimmed();
            const QString errorDescription =
                challenge.value(QStringLiteral("error_description")).trimmed();
            const QString errorCode = challenge.value(QStringLiteral("error")).trimmed();
            if (resourceMetadata.isEmpty() == false)
            {
                m_oauthResourceMetadataUrl = QUrl(resourceMetadata);
                emit logMessage(
                    QStringLiteral("OAuth challenge advertised protected-resource metadata: %1")
                        .arg(m_oauthResourceMetadataUrl.toString()));
            }
            if (errorCode.isEmpty() == false)
            {
                m_lastOAuthError = errorDescription.isEmpty()
                                       ? QStringLiteral("OAuth challenge error: %1").arg(errorCode)
                                       : QStringLiteral("OAuth challenge error: %1 (%2)")
                                             .arg(errorCode, errorDescription);
            }
            if (errorDescription.isEmpty() == false)
            {
                emit logMessage(
                    QStringLiteral("OAuth challenge error description: %1").arg(errorDescription));
            }
        }
        else if (((!it->buffer.trimmed().isEmpty()) == true))
        {
            m_lastOAuthError = QStringLiteral("OAuth authorization required: %1")
                                   .arg(QString::fromUtf8(it->buffer).trimmed());
        }
        m_accessToken.clear();
        m_accessTokenExpiration = {};
        emit logMessage(QStringLiteral("Cleared cached access token after 401; refreshToken=%1")
                            .arg(tokenPresenceSummary(m_refreshToken)));
        const bool authorized = ensureAuthorized();
        m_replyStates.erase(it);
        reply->deleteLater();
        if (authorized == false)
        {
            emit errorOccurred(m_lastOAuthError.isEmpty()
                                   ? QStringLiteral("MCP OAuth authorization failed.")
                                   : m_lastOAuthError);
            return;
        }

        emit logMessage(QStringLiteral("Retrying MCP HTTP request after OAuth authorization."));
        postPayload(retryPayload, true);
        return;
    }

    if (it->isSse)
    {
        processSseBuffer(reply, true);
    }
    else if (((!it->buffer.isEmpty()) == true))
    {
        emit logMessage(QStringLiteral("http <- %1").arg(formatPayloadForLog(it->buffer)));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(it->buffer, &parseError);
        if (((parseError.error != QJsonParseError::NoError) == true))
        {
            emit errorOccurred(QStringLiteral("Failed to parse MCP HTTP JSON message: %1")
                                   .arg(parseError.errorString()));
        }
        else
        {
            emitJsonDocument(reply, document, QStringLiteral("http response"));
        }
    }

    if (((statusCode == 404 && !m_sessionId.isEmpty()) == true))
    {
        emit logMessage(QStringLiteral("HTTP MCP session expired; clearing stored session id."));
        m_sessionId.clear();
    }

    if (reply->error() != QNetworkReply::NoError && it->emittedMessages == 0)
    {
        emit errorOccurred(QStringLiteral("MCP HTTP transport error (%1): %2")
                               .arg(statusCode)
                               .arg(reply->errorString()));
    }

    if (statusCode == 202 && it->emittedMessages == 0)
    {
        emit logMessage(QStringLiteral("HTTP request accepted with 202 and no response body."));
    }
    else if (statusCode != 202 && it->emittedMessages == 0 &&
             reply->error() == QNetworkReply::NoError)
    {
        emit errorOccurred(QStringLiteral("MCP HTTP reply completed without a JSON-RPC payload."));
    }

    m_replyStates.erase(it);
    reply->deleteLater();
}

void HttpTransport::processSseBuffer(QNetworkReply *reply, bool flush)
{
    while (true == true)
    {
        auto it = m_replyStates.find(reply);
        if (((it == m_replyStates.end()) == true))
        {
            return;
        }

        const qsizetype newlineIndex = it->buffer.indexOf('\n');
        if (newlineIndex < 0)
        {
            break;
        }

        QByteArray line = it->buffer.left(newlineIndex);
        it->buffer.remove(0, newlineIndex + 1);
        if (line.endsWith('\r') == true)
        {
            line.chop(1);
        }

        if (line.isEmpty() == true)
        {
            handleSseEvent(reply);
            continue;
        }

        if (line.startsWith(':') == true)
        {
            continue;
        }

        if (line.startsWith("event:") == true)
        {
            it->currentEventType = QString::fromUtf8(line.mid(6)).trimmed();
            continue;
        }

        if (line.startsWith("data:") == true)
        {
            QByteArray data = line.mid(5);
            if (data.startsWith(' ') == true)
            {
                data.remove(0, 1);
            }
            if (((!it->currentEventData.isEmpty()) == true))
            {
                it->currentEventData.append('\n');
            }
            it->currentEventData.append(data);
        }
    }

    auto it = m_replyStates.find(reply);
    if (((flush && it != m_replyStates.end() &&
          (!it->currentEventData.isEmpty() || !it->currentEventType.isEmpty())) == true))
    {
        handleSseEvent(reply);
    }
}

void HttpTransport::handleSseEvent(QNetworkReply *reply)
{
    auto it = m_replyStates.find(reply);
    if (((it == m_replyStates.end()) == true))
    {
        return;
    }

    const QByteArray data = it->currentEventData;
    const QString eventType = it->currentEventType;
    it->currentEventData.clear();
    it->currentEventType.clear();

    if (((data.trimmed().isEmpty()) == true))
    {
        return;
    }

    emit logMessage(QStringLiteral("sse event `%1` <- %2")
                        .arg(eventType.isEmpty() ? QStringLiteral("message") : eventType,
                             formatPayloadForLog(data)));

    if (((eventType == QStringLiteral("endpoint")) == true))
    {
        emit logMessage(QStringLiteral("Received legacy endpoint event: %1")
                            .arg(QString::fromUtf8(data).trimmed()));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (((parseError.error != QJsonParseError::NoError) == true))
    {
        emit errorOccurred(QStringLiteral("Failed to parse MCP SSE event JSON: %1")
                               .arg(parseError.errorString()));
        return;
    }

    emitJsonDocument(reply, document, QStringLiteral("http sse"));
}

void HttpTransport::emitJsonDocument(QNetworkReply *reply, const QJsonDocument &document,
                                     const QString &source)
{
    auto it = m_replyStates.find(reply);
    if (((it == m_replyStates.end()) == true))
    {
        return;
    }

    if (document.isObject() == true)
    {
        ++it->emittedMessages;
        emit messageReceived(document.object());
        return;
    }

    if (document.isArray() == true)
    {
        const QJsonArray messages = document.array();
        for (const QJsonValue &messageValue : messages)
        {
            if (messageValue.isObject() == false)
            {
                emit errorOccurred(
                    QStringLiteral("Received a non-object JSON-RPC batch item from %1.")
                        .arg(source));
                continue;
            }
            ++it->emittedMessages;
            emit messageReceived(messageValue.toObject());
        }
        return;
    }

    emit errorOccurred(
        QStringLiteral("Received an unsupported JSON payload from %1.").arg(source));
}

void HttpTransport::applySessionId(QNetworkReply *reply)
{
    const QByteArray header = reply->rawHeader("Mcp-Session-Id");
    if (header.isEmpty() == true)
    {
        return;
    }

    const QString sessionId = QString::fromUtf8(header).trimmed();
    if (((sessionId.isEmpty() || sessionId == m_sessionId) == true))
    {
        return;
    }

    m_sessionId = sessionId;
    emit logMessage(QStringLiteral("Stored MCP session id: %1").arg(m_sessionId));
}

bool HttpTransport::ensureAuthorized()
{
    if (((!m_config.oauthEnabled) == true))
    {
        return false;
    }

    m_lastOAuthError.clear();
    emit logMessage(
        QStringLiteral("Ensuring OAuth authorization: accessToken=%1 refreshToken=%2 expires=%3 "
                       "authorizationUrl=%4 tokenUrl=%5")
            .arg(tokenPresenceSummary(m_accessToken), tokenPresenceSummary(m_refreshToken),
                 expirySummary(m_accessTokenExpiration),
                 m_authorizationUrl.isEmpty() ? QStringLiteral("(unset)")
                                              : m_authorizationUrl.toString(),
                 m_tokenUrl.isEmpty() ? QStringLiteral("(unset)") : m_tokenUrl.toString()));

#if !QTMCP_HAS_NETWORKAUTH
    m_lastOAuthError = QStringLiteral("MCP OAuth requires Qt NetworkAuth support in qtmcp.");
    emit errorOccurred(m_lastOAuthError);
    return false;
#else
    if (((!m_refreshToken.isEmpty()) == true))
    {
        emit logMessage(QStringLiteral("Attempting OAuth refresh token flow."));
        if (refreshAccessToken() == true)
        {
            return true;
        }
        emit logMessage(QStringLiteral(
            "OAuth refresh failed; falling back to interactive authorization code flow."));
    }

    if (((!m_config.interactiveOAuthEnabled) == true))
    {
        if (m_lastOAuthError.isEmpty() == true)
        {
            m_lastOAuthError =
                m_lastAuthorizationRequired
                    ? QStringLiteral("OAuth authorization required.")
                    : QStringLiteral("Interactive OAuth authorization is disabled.");
        }
        emit logMessage(
            QStringLiteral("Interactive OAuth authorization is disabled for this flow."));
        return false;
    }

    return runAuthorizationCodeGrant();
#endif
}

bool HttpTransport::refreshAccessToken()
{
#if !QTMCP_HAS_NETWORKAUTH
    return false;
#else
    if (((m_registeredClientId.isEmpty() || !m_tokenUrl.isValid() || m_refreshToken.isEmpty()) ==
         true))
    {
        m_lastOAuthError = QStringLiteral("OAuth refresh cannot start because client "
                                          "registration, token URL, or refresh token is missing.");
        return false;
    }

    emit logMessage(
        QStringLiteral("Starting OAuth refresh: clientIdConfigured=%1 refreshToken=%2 tokenUrl=%3")
            .arg(m_registeredClientId.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"),
                 tokenPresenceSummary(m_refreshToken), m_tokenUrl.toString()));
    QOAuth2AuthorizationCodeFlow oauth(m_networkAccessManager);
    oauth.setClientIdentifier(m_registeredClientId);
    if (((!m_registeredClientSecret.isEmpty()) == true))
    {
        oauth.setClientIdentifierSharedKey(m_registeredClientSecret);
    }
    oauth.setTokenUrl(m_tokenUrl);
    if (m_authorizationUrl.isValid() == true)
    {
        oauth.setAuthorizationUrl(m_authorizationUrl);
    }
    oauth.setRefreshToken(m_refreshToken);
    if (((!m_accessToken.isEmpty()) == true))
    {
        oauth.setToken(m_accessToken);
    }
    if (((!m_config.oauthScopes.isEmpty()) == true))
    {
        oauth.setRequestedScopeTokens(requestedScopeTokens(m_config.oauthScopes));
    }
    oauth.setModifyParametersFunction(
        [this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            applyOAuthParameters(stage, parameters);
        });
    oauth.setNetworkRequestModifier(this, [this](QNetworkRequest &request, QAbstractOAuth::Stage) {
        if (((m_config.requestTimeoutMs > 0) == true))
        {
            request.setTransferTimeout(m_config.requestTimeoutMs);
        }
    });

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QString failure;
    bool success = false;

    QObject::connect(&oauth, &QAbstractOAuth::tokenChanged, &loop, [&](const QString &token) {
        emit logMessage(
            QStringLiteral("OAuth refresh tokenChanged: %1").arg(tokenPresenceSummary(token)));
        if (token.isEmpty() == false)
        {
            success = true;
            loop.quit();
        }
    });
    QObject::connect(&oauth, &QAbstractOAuth::granted, &loop, [&]() {
        emit logMessage(
            QStringLiteral("OAuth refresh granted signal: token=%1 refreshToken=%2 expires=%3")
                .arg(tokenPresenceSummary(oauth.token()),
                     tokenPresenceSummary(oauth.refreshToken()),
                     expirySummary(oauth.expirationAt())));
        success = true;
        loop.quit();
    });
    QObject::connect(
        &oauth, &QAbstractOAuth::requestFailed, &loop, [&](QAbstractOAuth::Error error) {
            failure =
                QStringLiteral("OAuth refresh request failed: %1").arg(static_cast<int>(error));
            loop.quit();
        });
    QObject::connect(
        &oauth, &QAbstractOAuth2::serverReportedErrorOccurred, &loop,
        [&](const QString &error, const QString &description, const QUrl &) {
            failure =
                QStringLiteral("OAuth refresh server error: %1 (%2)").arg(error, description);
            loop.quit();
        });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(m_config.requestTimeoutMs > 0 ? m_config.requestTimeoutMs
                                              : kOAuthInteractiveTimeoutMs);
    oauth.refreshTokens();
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (success == false)
    {
        m_lastOAuthError =
            failure.isEmpty() ? QStringLiteral("OAuth refresh timed out.") : failure;
        emit logMessage(m_lastOAuthError);
        return false;
    }

    m_accessToken = oauth.token();
    if (((!oauth.refreshToken().isEmpty()) == true))
    {
        m_refreshToken = oauth.refreshToken();
    }
    m_accessTokenExpiration = oauth.expirationAt();
    if (m_accessToken.isEmpty() == true)
    {
        m_lastOAuthError =
            QStringLiteral("OAuth refresh finished without returning an access token.");
        emit logMessage(m_lastOAuthError);
        return false;
    }
    emit logMessage(
        QStringLiteral("OAuth refresh result: accessToken=%1 refreshToken=%2 expires=%3")
            .arg(tokenPresenceSummary(m_accessToken), tokenPresenceSummary(m_refreshToken),
                 expirySummary(m_accessTokenExpiration)));
    saveCachedOAuthState();
    emit logMessage(QStringLiteral("OAuth refresh succeeded."));
    return true;
#endif
}

bool HttpTransport::runAuthorizationCodeGrant()
{
#if !QTMCP_HAS_NETWORKAUTH
    return false;
#else
    QOAuthHttpServerReplyHandler replyHandler;
    replyHandler.setCallbackPath(QStringLiteral("/oauth2/callback"));
    replyHandler.setCallbackText(
        QStringLiteral("Authorization completed. You can return to Qt Creator."));
    QString listenDetails;
    if (startOAuthCallbackListener(&replyHandler, &listenDetails) == false)
    {
        m_lastOAuthError = listenDetails.isEmpty()
                               ? QStringLiteral("Failed to start local OAuth callback listener.")
                               : listenDetails;
        emit errorOccurred(m_lastOAuthError);
        return false;
    }
    if (listenDetails.isEmpty() == false)
    {
        emit logMessage(listenDetails);
    }
    emit logMessage(
        QStringLiteral("OAuth callback configuration: callback=%1").arg(replyHandler.callback()));

    OAuthEndpoints endpoints;
    QString discoveryLog;
    ProtectedResourceMetadata protectedResource;
    QString protectedResourceLog;
    const QUrl protectedResourceMetadataUrl =
        m_oauthResourceMetadataUrl.isValid() && !m_oauthResourceMetadataUrl.isEmpty()
            ? m_oauthResourceMetadataUrl
            : defaultProtectedResourceMetadataUrl(m_config.endpoint);
    const bool haveProtectedResourceMetadata = loadProtectedResourceMetadata(
        m_networkAccessManager, m_config, protectedResourceMetadataUrl, &protectedResource,
        &protectedResourceLog);
    if (protectedResourceLog.isEmpty() == false)
    {
        emit logMessage(protectedResourceLog);
    }

    if (haveProtectedResourceMetadata && !protectedResource.resource.trimmed().isEmpty())
    {
        m_oauthResource = protectedResource.resource.trimmed();
    }

    const QUrl authorizationMetadataUrl =
        haveProtectedResourceMetadata && !protectedResource.authorizationServers.isEmpty()
            ? oauthAuthorizationMetadataUrl(protectedResource.authorizationServers.constFirst())
            : oauthAuthorizationMetadataUrl(authorizationBaseUrl(m_config.endpoint));

    if (((!discoverOAuthEndpoints(m_networkAccessManager, m_config, authorizationMetadataUrl,
                                  &endpoints, &discoveryLog)) == true))
    {
        m_lastOAuthError = QStringLiteral("Failed to resolve OAuth endpoints.");
        emit errorOccurred(m_lastOAuthError);
        return false;
    }
    if (discoveryLog.isEmpty() == false)
    {
        emit logMessage(discoveryLog);
    }

    m_authorizationUrl = endpoints.authorizationEndpoint;
    m_tokenUrl = endpoints.tokenEndpoint;
    emit logMessage(
        QStringLiteral(
            "Resolved OAuth endpoints: authorization=%1 token=%2 registration=%3 resource=%4")
            .arg(m_authorizationUrl.toString(), m_tokenUrl.toString(),
                 endpoints.registrationEndpoint.isEmpty()
                     ? QStringLiteral("(unset)")
                     : endpoints.registrationEndpoint.toString(),
                 m_oauthResource.isEmpty() ? QStringLiteral("(unset)") : m_oauthResource));

    if (((!m_config.oauthClientId.isEmpty()) == true))
    {
        m_registeredClientId = m_config.oauthClientId;
    }
    if (((!m_config.oauthClientSecret.isEmpty()) == true))
    {
        m_registeredClientSecret = m_config.oauthClientSecret;
    }

    if (m_registeredClientId.isEmpty() == true)
    {
        if (endpoints.registrationEndpoint.isValid() == false)
        {
            m_lastOAuthError = QStringLiteral("OAuth server does not expose dynamic client "
                                              "registration and no oauthClientId was configured.");
            emit errorOccurred(m_lastOAuthError);
            return false;
        }

        RegisteredOAuthClient registeredClient;
        QString registrationError;
        if (((!registerOAuthClient(m_networkAccessManager, m_config,
                                   endpoints.registrationEndpoint, replyHandler.callback(),
                                   &registeredClient, &registrationError)) == true))
        {
            m_lastOAuthError = registrationError;
            emit errorOccurred(m_lastOAuthError);
            return false;
        }

        m_registeredClientId = registeredClient.clientId;
        m_registeredClientSecret = registeredClient.clientSecret;
        emit logMessage(QStringLiteral("Registered OAuth client dynamically."));
    }

    QOAuth2AuthorizationCodeFlow oauth(m_networkAccessManager);
    oauth.setReplyHandler(&replyHandler);
    oauth.setAuthorizationUrl(m_authorizationUrl);
    oauth.setTokenUrl(m_tokenUrl);
    oauth.setClientIdentifier(m_registeredClientId);
    if (((!m_registeredClientSecret.isEmpty()) == true))
    {
        oauth.setClientIdentifierSharedKey(m_registeredClientSecret);
    }
    oauth.setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    if (((!m_config.oauthScopes.isEmpty()) == true))
    {
        oauth.setRequestedScopeTokens(requestedScopeTokens(m_config.oauthScopes));
    }
    oauth.setModifyParametersFunction(
        [this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            applyOAuthParameters(stage, parameters);
        });
    oauth.setNetworkRequestModifier(this, [this](QNetworkRequest &request, QAbstractOAuth::Stage) {
        if (((m_config.requestTimeoutMs > 0) == true))
        {
            request.setTransferTimeout(m_config.requestTimeoutMs);
        }
    });

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QString failure;
    bool success = false;

    QObject::connect(&oauth, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
        emit logMessage(
            QStringLiteral("Opening browser for OAuth authorization: %1").arg(url.toString()));
        if (QDesktopServices::openUrl(url) == false)
        {
            emit logMessage(
                QStringLiteral("Failed to open browser automatically. Open this URL manually: %1")
                    .arg(url.toString()));
        }
    });
    QObject::connect(&oauth, &QAbstractOAuth::tokenChanged, &loop, [&](const QString &token) {
        emit logMessage(QStringLiteral("OAuth authorization tokenChanged: %1")
                            .arg(tokenPresenceSummary(token)));
        if (token.isEmpty() == false)
        {
            success = true;
            loop.quit();
        }
    });
    QObject::connect(&oauth, &QAbstractOAuth::granted, &loop, [&]() {
        emit logMessage(
            QStringLiteral(
                "OAuth authorization granted signal: token=%1 refreshToken=%2 expires=%3")
                .arg(tokenPresenceSummary(oauth.token()),
                     tokenPresenceSummary(oauth.refreshToken()),
                     expirySummary(oauth.expirationAt())));
        success = true;
        loop.quit();
    });
    QObject::connect(&oauth, &QAbstractOAuth::requestFailed, &loop,
                     [&](QAbstractOAuth::Error error) {
                         failure = QStringLiteral("OAuth authorization request failed: %1")
                                       .arg(static_cast<int>(error));
                         loop.quit();
                     });
    QObject::connect(&oauth, &QAbstractOAuth2::serverReportedErrorOccurred, &loop,
                     [&](const QString &error, const QString &description, const QUrl &) {
                         failure =
                             QStringLiteral("OAuth server error: %1 (%2)").arg(error, description);
                         loop.quit();
                     });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(kOAuthInteractiveTimeoutMs);
    emit logMessage(
        QStringLiteral("Starting OAuth authorization code flow: clientIdConfigured=%1 "
                       "clientSecretConfigured=%2 scopes=%3")
            .arg(m_registeredClientId.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"),
                 m_registeredClientSecret.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"),
                 m_config.oauthScopes.join(QStringLiteral(","))));
    oauth.grant();
    loop.exec();

    if (success == false)
    {
        m_lastOAuthError =
            failure.isEmpty() ? QStringLiteral("OAuth authorization timed out.") : failure;
        emit errorOccurred(m_lastOAuthError);
        return false;
    }

    m_accessToken = oauth.token();
    m_refreshToken = oauth.refreshToken();
    m_accessTokenExpiration = oauth.expirationAt();
    if (m_accessToken.isEmpty() == true)
    {
        m_lastOAuthError =
            QStringLiteral("OAuth authorization finished without returning an access token.");
        emit errorOccurred(m_lastOAuthError);
        return false;
    }
    emit logMessage(
        QStringLiteral("OAuth authorization result: accessToken=%1 refreshToken=%2 expires=%3")
            .arg(tokenPresenceSummary(m_accessToken), tokenPresenceSummary(m_refreshToken),
                 expirySummary(m_accessTokenExpiration)));
    saveCachedOAuthState();
    emit logMessage(QStringLiteral("OAuth authorization succeeded."));
    return true;
#endif
}

#if QTMCP_HAS_NETWORKAUTH
void HttpTransport::applyOAuthParameters(QAbstractOAuth::Stage stage,
                                         QMultiMap<QString, QVariant> *parameters) const
{
    Q_UNUSED(stage);
    if (((parameters == nullptr || m_oauthResource.trimmed().isEmpty()) == true))
    {
        return;
    }

    parameters->insert(QStringLiteral("resource"), m_oauthResource.trimmed());
}
#endif

void HttpTransport::loadCachedOAuthState()
{
    if (((!m_config.oauthEnabled) == true))
    {
        return;
    }

    const QString cacheKey = oauthCacheKey(m_config);
    emit logMessage(
        QStringLiteral("Looking up OAuth cache: endpoint=%1 clientIdConfigured=%2 scopes=%3")
            .arg(m_config.endpoint.toString(),
                 m_config.oauthClientId.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"),
                 m_config.oauthScopes.join(QStringLiteral(","))));
    auto it = oauthCache().constFind(cacheKey);
    if (it == oauthCache().cend())
    {
        OAuthCacheEntry persistedEntry;
        QString persistentError;
        if (loadPersistentOAuthCacheEntry(cacheKey, &persistedEntry, &persistentError) == true)
        {
            oauthCache().insert(cacheKey, persistedEntry);
            it = oauthCache().constFind(cacheKey);
            emit logMessage(QStringLiteral("Loaded OAuth state from persistent cache file: %1")
                                .arg(persistentOAuthCachePath()));
        }
        else
        {
            if (persistentError.isEmpty() == false)
            {
                emit logMessage(
                    QStringLiteral("Persistent OAuth cache read failed: %1").arg(persistentError));
            }
            if (((!m_config.oauthClientId.isEmpty()) == true))
            {
                m_registeredClientId = m_config.oauthClientId;
            }
            if (((!m_config.oauthClientSecret.isEmpty()) == true))
            {
                m_registeredClientSecret = m_config.oauthClientSecret;
            }
            emit logMessage(QStringLiteral("OAuth cache miss."));
            return;
        }
    }

    m_accessToken = it->accessToken;
    m_refreshToken = it->refreshToken;
    m_accessTokenExpiration = it->expiration;
    m_authorizationUrl = it->authorizationUrl;
    m_tokenUrl = it->tokenUrl;
    m_registeredClientId =
        m_config.oauthClientId.isEmpty() ? it->clientId : m_config.oauthClientId;
    m_registeredClientSecret =
        m_config.oauthClientSecret.isEmpty() ? it->clientSecret : m_config.oauthClientSecret;
    m_oauthResource = it->resource;
    m_oauthResourceMetadataUrl = it->resourceMetadataUrl;

    emit logMessage(
        QStringLiteral(
            "Loaded cached OAuth state for %1: accessToken=%2 refreshToken=%3 expires=%4")
            .arg(m_config.endpoint.toString(), tokenPresenceSummary(m_accessToken),
                 tokenPresenceSummary(m_refreshToken), expirySummary(m_accessTokenExpiration)));
}

void HttpTransport::saveCachedOAuthState() const
{
    if (((!m_config.oauthEnabled) == true))
    {
        return;
    }

    OAuthCacheEntry entry;
    entry.accessToken = m_accessToken;
    entry.refreshToken = m_refreshToken;
    entry.expiration = m_accessTokenExpiration;
    entry.authorizationUrl = m_authorizationUrl;
    entry.tokenUrl = m_tokenUrl;
    entry.clientId = m_registeredClientId;
    entry.clientSecret = m_registeredClientSecret;
    entry.resource = m_oauthResource;
    entry.resourceMetadataUrl = m_oauthResourceMetadataUrl;
    oauthCache().insert(oauthCacheKey(m_config), entry);
    emit const_cast<HttpTransport *>(this)->logMessage(
        QStringLiteral(
            "Saved OAuth state to cache for %1: accessToken=%2 refreshToken=%3 expires=%4")
            .arg(m_config.endpoint.toString(), tokenPresenceSummary(m_accessToken),
                 tokenPresenceSummary(m_refreshToken), expirySummary(m_accessTokenExpiration)));
    QString persistentError;
    if (((!savePersistentOAuthCacheEntry(oauthCacheKey(m_config), entry, &persistentError)) ==
         true))
    {
        emit const_cast<HttpTransport *>(this)->logMessage(
            QStringLiteral("Failed to persist OAuth cache: %1").arg(persistentError));
    }
    else
    {
        emit const_cast<HttpTransport *>(this)->logMessage(
            QStringLiteral("Saved OAuth state to persistent cache file: %1")
                .arg(persistentOAuthCachePath()));
    }
}

}  // namespace qtmcp
