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

constexpr int k_max_logged_payload_chars = 2000;

QString format_payload_for_log(const QByteArray &payload)
{
    QString text = QString::fromUtf8(payload);
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    if (text.size() > k_max_logged_payload_chars)
    {
        text = text.left(k_max_logged_payload_chars);
        text += QStringLiteral("... [truncated]");
    }
    return text;
}

QString normalized_content_type(QNetworkReply *reply)
{
    const QString raw = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const qsizetype separator = raw.indexOf(QLatin1Char(';'));
    return (separator >= 0 ? raw.left(separator) : raw).trimmed().toLower();
}

QString scopes_cache_key(QStringList scopes)
{
    std::sort(scopes.begin(), scopes.end());
    return scopes.join(QLatin1Char(' '));
}

QString oauth_cache_key(const http_transport_config_t &config)
{
    return QStringLiteral("%1|%2|%3")
        .arg(config.endpoint.toString(QUrl::FullyEncoded), config.oauth_client_id,
             scopes_cache_key(config.oauth_scopes));
}

QString token_presence_summary(const QString &token)
{
    return token.isEmpty() ? QStringLiteral("absent")
                           : QStringLiteral("present(len=%1)").arg(token.size());
}

QString expiry_summary(const QDateTime &expiration)
{
    return expiration.isValid() ? expiration.toString(Qt::ISODate) : QStringLiteral("none");
}

struct o_auth_cache_entry_t
{
    QString access_token;
    QString refresh_token;
    QDateTime expiration;
    QUrl authorization_url;
    QUrl token_url;
    QString client_id;
    QString client_secret;
    QString resource;
    QUrl resource_metadata_url;
};

QHash<QString, o_auth_cache_entry_t> &oauth_cache()
{
    static QHash<QString, o_auth_cache_entry_t> cache;
    return cache;
}

QString persistent_o_auth_cache_path()
{
    QString base_path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (((base_path.trimmed().isEmpty()) == true))
    {
        base_path = QDir::homePath() + QStringLiteral("/.config");
    }
    return QDir(base_path).filePath(QStringLiteral("qtmcp/oauth-cache.json"));
}

QJsonObject oauth_cache_entry_to_json(const o_auth_cache_entry_t &entry)
{
    QJsonObject object;
    if (((!entry.access_token.isEmpty()) == true))
    {
        object.insert(QStringLiteral("accessToken"), entry.access_token);
    }
    if (((!entry.refresh_token.isEmpty()) == true))
    {
        object.insert(QStringLiteral("refreshToken"), entry.refresh_token);
    }
    if (entry.expiration.isValid() == true)
    {
        object.insert(QStringLiteral("expiration"), entry.expiration.toString(Qt::ISODate));
    }
    if (((entry.authorization_url.isValid() && !entry.authorization_url.isEmpty()) == true))
    {
        object.insert(QStringLiteral("authorizationUrl"), entry.authorization_url.toString());
    }
    if (((entry.token_url.isValid() && !entry.token_url.isEmpty()) == true))
    {
        object.insert(QStringLiteral("tokenUrl"), entry.token_url.toString());
    }
    if (((!entry.client_id.isEmpty()) == true))
    {
        object.insert(QStringLiteral("clientId"), entry.client_id);
    }
    if (((!entry.client_secret.isEmpty()) == true))
    {
        object.insert(QStringLiteral("clientSecret"), entry.client_secret);
    }
    if (((!entry.resource.isEmpty()) == true))
    {
        object.insert(QStringLiteral("resource"), entry.resource);
    }
    if (((entry.resource_metadata_url.isValid() && !entry.resource_metadata_url.isEmpty()) ==
         true))
    {
        object.insert(QStringLiteral("resourceMetadataUrl"),
                      entry.resource_metadata_url.toString());
    }
    return object;
}

bool oauth_cache_entry_from_json(const QJsonObject &object, o_auth_cache_entry_t *entry)
{
    if (((entry == nullptr) == true))
    {
        return false;
    }

    o_auth_cache_entry_t parsed;
    parsed.access_token = object.value(QStringLiteral("accessToken")).toString();
    parsed.refresh_token = object.value(QStringLiteral("refreshToken")).toString();
    parsed.expiration =
        QDateTime::fromString(object.value(QStringLiteral("expiration")).toString(), Qt::ISODate);
    parsed.authorization_url = QUrl(object.value(QStringLiteral("authorizationUrl")).toString());
    parsed.token_url = QUrl(object.value(QStringLiteral("tokenUrl")).toString());
    parsed.client_id = object.value(QStringLiteral("clientId")).toString();
    parsed.client_secret = object.value(QStringLiteral("clientSecret")).toString();
    parsed.resource = object.value(QStringLiteral("resource")).toString();
    parsed.resource_metadata_url =
        QUrl(object.value(QStringLiteral("resourceMetadataUrl")).toString());
    *entry = parsed;
    return true;
}

bool load_persistent_o_auth_cache_entry(const QString &cache_key, o_auth_cache_entry_t *entry,
                                        QString *error = nullptr)
{
    QFile file(persistent_o_auth_cache_path());
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

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (((parse_error.error != QJsonParseError::NoError || !document.isObject()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to parse OAuth cache file: %1")
                         .arg(parse_error.error == QJsonParseError::NoError
                                  ? QStringLiteral("root must be an object")
                                  : parse_error.errorString());
        }
        return false;
    }

    const QJsonValue value = document.object().value(cache_key);
    if (value.isObject() == false)
    {
        return false;
    }
    return oauth_cache_entry_from_json(value.toObject(), entry);
}

bool save_persistent_o_auth_cache_entry(const QString &cache_key,
                                        const o_auth_cache_entry_t &entry,
                                        QString *error = nullptr)
{
    const QString path = persistent_o_auth_cache_path();
    QJsonObject root;

    QFile existing_file(path);
    if (existing_file.exists() == true)
    {
        if (existing_file.open(QIODevice::ReadOnly) == false)
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("Failed to open OAuth cache file for reading: %1")
                             .arg(existing_file.errorString());
            }
            return false;
        }

        QJsonParseError parse_error;
        const QJsonDocument document =
            QJsonDocument::fromJson(existing_file.readAll(), &parse_error);
        if (((parse_error.error != QJsonParseError::NoError || !document.isObject()) == true))
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("Failed to parse OAuth cache file: %1")
                             .arg(parse_error.error == QJsonParseError::NoError
                                      ? QStringLiteral("root must be an object")
                                      : parse_error.errorString());
            }
            return false;
        }
        root = document.object();
    }

    root.insert(cache_key, oauth_cache_entry_to_json(entry));

    const QFileInfo file_info(path);
    if (((!QDir().mkpath(file_info.absolutePath())) == true))
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

QMap<QString, QString> parse_bearer_challenge_parameters(const QByteArray &www_authenticate_header)
{
    QMap<QString, QString> parameters;
    const QString header = QString::fromUtf8(www_authenticate_header);
    const qsizetype bearer_index = header.indexOf(QRegularExpression(
        QStringLiteral("(^|\\s|,)Bearer\\s+"), QRegularExpression::CaseInsensitiveOption));
    if (bearer_index < 0)
    {
        return parameters;
    }

    const qsizetype params_start = header.indexOf(QLatin1Char(' '), bearer_index);
    if (params_start < 0)
    {
        return parameters;
    }

    const QString params = header.mid(params_start + 1);
    static const QRegularExpression pair_expression(
        QStringLiteral(R"re(([A-Za-z_][A-Za-z0-9_-]*)\s*=\s*"([^"]*)")re"));
    auto it = pair_expression.globalMatch(params);
    while (it.hasNext())
    {
        const auto match = it.next();
        parameters.insert(match.captured(1), match.captured(2));
    }

    return parameters;
}

#if QTMCP_HAS_NETWORKAUTH

constexpr int k_o_auth_interactive_timeout_ms = 5 * 60 * 1000;

QUrl authorization_base_url(const QUrl &endpoint)
{
    QUrl base = endpoint.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    base.setPath(QString());
    return base;
}

QUrl append_path(const QUrl &base, const QString &path)
{
    QUrl url(base);
    url.setPath(path);
    return url;
}

struct sync_http_response_t
{
    int status_code = 0;
    QNetworkReply::NetworkError network_error = QNetworkReply::NoError;
    QString error_string;
    QByteArray body;
    QJsonDocument json_body;
    bool has_json_body = false;
};

sync_http_response_t perform_sync_request(QNetworkAccessManager *network_access_manager,
                                          QNetworkRequest request, const QByteArray &verb,
                                          const QByteArray &body = {})
{
    sync_http_response_t result;
    QNetworkReply *reply = nullptr;

    if (((verb == QByteArrayLiteral("GET")) == true))
    {
        reply = network_access_manager->get(request);
    }
    else if (((verb == QByteArrayLiteral("POST")) == true))
    {
        reply = network_access_manager->post(request, body);
    }
    else
    {
        return result;
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    result.status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.network_error = reply->error();
    result.error_string = reply->errorString();
    result.body = reply->readAll();

    if (((!result.body.isEmpty()) == true))
    {
        QJsonParseError parse_error;
        const QJsonDocument json = QJsonDocument::fromJson(result.body, &parse_error);
        if (((parse_error.error == QJsonParseError::NoError) == true))
        {
            result.json_body = json;
            result.has_json_body = true;
        }
    }

    reply->deleteLater();
    return result;
}

struct o_auth_endpoints_t
{
    QUrl authorization_endpoint;
    QUrl token_endpoint;
    QUrl registration_endpoint;
};

struct protected_resource_metadata_t
{
    QString resource;
    QList<QUrl> authorization_servers;
    QUrl metadata_url;
};

struct registered_o_auth_client_t
{
    QString client_id;
    QString client_secret;
};

struct o_auth_listen_attempt_t
{
    QString callback_host;
    QHostAddress address;
    QString label;
};

QSet<QByteArray> requested_scope_tokens(const QStringList &scopes)
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

QUrl oauth_authorization_metadata_url(const QUrl &authorization_server)
{
    QUrl base(authorization_server);
    base.setQuery(QString());
    base.setFragment(QString());
    return append_path(base, QStringLiteral("/.well-known/oauth-authorization-server"));
}

QUrl default_protected_resource_metadata_url(const QUrl &endpoint)
{
    return append_path(authorization_base_url(endpoint),
                       QStringLiteral("/.well-known/oauth-protected-resource"));
}

bool load_protected_resource_metadata(QNetworkAccessManager *network_access_manager,
                                      const http_transport_config_t &config,
                                      const QUrl &metadata_url,
                                      protected_resource_metadata_t *metadata,
                                      QString *log_message)
{
    if (((!metadata_url.isValid() || metadata_url.isEmpty()) == true))
    {
        return false;
    }

    QNetworkRequest request(metadata_url);
    request.setRawHeader("Accept", "application/json");
    if (((config.request_timeout_ms > 0) == true))
    {
        request.setTransferTimeout(config.request_timeout_ms);
    }

    const sync_http_response_t response =
        perform_sync_request(network_access_manager, request, QByteArrayLiteral("GET"));
    if (response.network_error != QNetworkReply::NoError || response.status_code < 200 ||
        response.status_code >= 300 || !response.has_json_body || !response.json_body.isObject())
    {
        return false;
    }

    const QJsonObject root = response.json_body.object();
    metadata->metadata_url = metadata_url;
    metadata->resource = root.value(QStringLiteral("resource")).toString();
    const QJsonArray authorization_servers =
        root.value(QStringLiteral("authorization_servers")).toArray();
    for (const QJsonValue &value : authorization_servers)
    {
        const QUrl url(value.toString());
        if (url.isValid() && !url.isEmpty())
        {
            metadata->authorization_servers.append(url);
        }
    }

    if (((log_message != nullptr) == true))
    {
        *log_message = QStringLiteral("Loaded OAuth protected-resource metadata from %1")
                           .arg(metadata_url.toString());
    }
    return true;
}

bool discover_o_auth_endpoints(QNetworkAccessManager *network_access_manager,
                               const http_transport_config_t &config, const QUrl &metadata_url,
                               o_auth_endpoints_t *endpoints, QString *log_message)
{
    const QUrl resolved_metadata_url =
        metadata_url.isValid() && !metadata_url.isEmpty()
            ? metadata_url
            : oauth_authorization_metadata_url(authorization_base_url(config.endpoint));

    QNetworkRequest request(resolved_metadata_url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("MCP-Protocol-Version", config.protocol_version.toUtf8());
    if (((config.request_timeout_ms > 0) == true))
    {
        request.setTransferTimeout(config.request_timeout_ms);
    }

    const sync_http_response_t response =
        perform_sync_request(network_access_manager, request, QByteArrayLiteral("GET"));

    if (response.network_error == QNetworkReply::NoError && response.status_code >= 200 &&
        response.status_code < 300 && response.has_json_body && response.json_body.isObject())
    {
        const QJsonObject root = response.json_body.object();
        endpoints->authorization_endpoint =
            QUrl(root.value(QStringLiteral("authorization_endpoint")).toString());
        endpoints->token_endpoint = QUrl(root.value(QStringLiteral("token_endpoint")).toString());
        endpoints->registration_endpoint =
            QUrl(root.value(QStringLiteral("registration_endpoint")).toString());

        if (((log_message != nullptr) == true))
        {
            *log_message = QStringLiteral("Discovered OAuth metadata at %1")
                               .arg(resolved_metadata_url.toString());
        }
        return endpoints->authorization_endpoint.isValid() && endpoints->token_endpoint.isValid();
    }

    const QUrl auth_base = authorization_base_url(config.endpoint);
    endpoints->authorization_endpoint = append_path(auth_base, QStringLiteral("/authorize"));
    endpoints->token_endpoint = append_path(auth_base, QStringLiteral("/token"));
    endpoints->registration_endpoint = QUrl();
    if (((log_message != nullptr) == true))
    {
        *log_message =
            QStringLiteral("OAuth metadata unavailable at %1, using default authorization/token "
                           "endpoint paths without assuming dynamic client registration.")
                .arg(resolved_metadata_url.toString());
    }
    return true;
}

bool register_o_auth_client(QNetworkAccessManager *network_access_manager,
                            const http_transport_config_t &config,
                            const QUrl &registration_endpoint, const QString &callback_url,
                            registered_o_auth_client_t *client, QString *error)
{
    QNetworkRequest request(registration_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (((config.request_timeout_ms > 0) == true))
    {
        request.setTransferTimeout(config.request_timeout_ms);
    }

    const QJsonObject body{
        {QStringLiteral("client_name"), config.oauth_client_name},
        {QStringLiteral("redirect_uris"), QJsonArray{callback_url}},
        {QStringLiteral("grant_types"),
         QJsonArray{QStringLiteral("authorization_code"), QStringLiteral("refresh_token")}},
        {QStringLiteral("response_types"), QJsonArray{QStringLiteral("code")}},
        {QStringLiteral("token_endpoint_auth_method"),
         config.oauth_client_secret.isEmpty() ? QStringLiteral("none")
                                              : QStringLiteral("client_secret_post")}};

    const sync_http_response_t response =
        perform_sync_request(network_access_manager, request, QByteArrayLiteral("POST"),
                             QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (response.network_error != QNetworkReply::NoError || response.status_code < 200 ||
        response.status_code >= 300 || !response.has_json_body || !response.json_body.isObject())
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Dynamic client registration failed at %1 (HTTP %2): %3")
                         .arg(registration_endpoint.toString())
                         .arg(response.status_code)
                         .arg(response.error_string);
        }
        return false;
    }

    const QJsonObject root = response.json_body.object();
    client->client_id = root.value(QStringLiteral("client_id")).toString();
    client->client_secret = root.value(QStringLiteral("client_secret")).toString();
    if (client->client_id.isEmpty())
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Dynamic client registration returned no client_id.");
        }
        return false;
    }

    return true;
}

bool start_o_auth_callback_listener(QOAuthHttpServerReplyHandler *reply_handler, QString *details)
{
    if (((reply_handler == nullptr) == true))
    {
        return false;
    }

    const std::array<o_auth_listen_attempt_t, 4> attempts{{
        {QStringLiteral("127.0.0.1"), QHostAddress(QStringLiteral("127.0.0.1")),
         QStringLiteral("127.0.0.1")},
        {QStringLiteral("localhost"), QHostAddress(QHostAddress::LocalHost),
         QStringLiteral("localhost / LocalHost")},
        {QStringLiteral("::1"), QHostAddress(QHostAddress::LocalHostIPv6), QStringLiteral("::1")},
        {QStringLiteral("127.0.0.1"), QHostAddress(QHostAddress::AnyIPv4),
         QStringLiteral("AnyIPv4 via 127.0.0.1")},
    }};

    QStringList failure_labels;
    for (const o_auth_listen_attempt_t &attempt : attempts)
    {
        reply_handler->close();
        reply_handler->setCallbackHost(attempt.callback_host);
        if (reply_handler->listen(attempt.address, 0))
        {
            if (details != nullptr)
            {
                *details = QStringLiteral("Listening for OAuth callback on %1")
                               .arg(reply_handler->callback());
            }
            return true;
        }
        failure_labels.append(attempt.label);
    }

    if (((details != nullptr) == true))
    {
        *details = QStringLiteral("Failed to start local OAuth callback listener. Tried %1.")
                       .arg(failure_labels.join(QStringLiteral(", ")));
    }
    return false;
}

#endif

}  // namespace

struct http_transport_t::ReplyState
{
    QByteArray buffer;
    QByteArray current_event_data;
    QString current_event_type;
    QByteArray request_payload;
    bool is_sse = false;
    bool oauth_retried = false;
    int emitted_messages = 0;
};

http_transport_t::http_transport_t(http_transport_config_t config, QObject *parent)
    : transport_t(parent), transport_config(std::move(config)),
      network_access_manager(new QNetworkAccessManager(this))
{
}

http_transport_t::~http_transport_t() = default;

QString http_transport_t::transport_name() const
{
    return QStringLiteral("http");
}

transport_t::state_t http_transport_t::state() const
{
    return this->transport_state;
}

const http_transport_config_t &http_transport_t::config() const
{
    return this->transport_config;
}

QString http_transport_t::last_o_auth_error() const
{
    return this->last_o_auth_error_message;
}

bool http_transport_t::last_authorization_required() const
{
    return this->authorization_required;
}

bool http_transport_t::has_cached_o_auth_credentials() const
{
    return !this->access_token.isEmpty() || !this->refresh_token.isEmpty();
}

bool http_transport_t::authorize(QString *error_message)
{
    emit log_message(
        QStringLiteral(
            "Manual OAuth authorization requested: accessToken=%1 refreshToken=%2 expires=%3")
            .arg(token_presence_summary(this->access_token),
                 token_presence_summary(this->refresh_token),
                 expiry_summary(this->access_token_expiration)));
    this->authorization_required = false;
    if (((this->transport_state == state_t::DISCONNECTED) == true))
    {
        this->start();
    }

    if (((this->transport_state != state_t::CONNECTED) == true))
    {
        if (((error_message != nullptr) == true))
        {
            *error_message = this->last_o_auth_error_message.isEmpty()
                                 ? QStringLiteral("MCP HTTP transport is not connected.")
                                 : this->last_o_auth_error_message;
        }
        return false;
    }

    const bool authorized = this->ensure_authorized();
    if (((!authorized && this->last_o_auth_error_message.isEmpty()) == true))
    {
        this->last_o_auth_error_message =
            QStringLiteral("OAuth authorization did not complete successfully.");
    }
    if (((error_message != nullptr) == true))
    {
        *error_message = authorized ? QString()
                                    : (this->last_o_auth_error_message.isEmpty()
                                           ? QStringLiteral("MCP OAuth authorization failed.")
                                           : this->last_o_auth_error_message);
    }
    return authorized;
}

void http_transport_t::start()
{
    if (((this->transport_state != state_t::DISCONNECTED) == true))
    {
        return;
    }

    if (((!this->transport_config.endpoint.isValid() ||
          this->transport_config.endpoint.isEmpty()) == true))
    {
        this->last_o_auth_error_message =
            QStringLiteral("MCP HTTP transport requires a valid endpoint URL.");
        emit error_occurred(this->last_o_auth_error_message);
        return;
    }

    const QString scheme = this->transport_config.endpoint.scheme().toLower();
    if (((scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) == true))
    {
        this->last_o_auth_error_message =
            QStringLiteral("MCP HTTP transport only supports http/https URLs.");
        emit error_occurred(this->last_o_auth_error_message);
        return;
    }

    this->load_cached_o_auth_state();
    this->last_o_auth_error_message.clear();
    this->authorization_required = false;
    emit log_message(QStringLiteral("HTTP transport ready: endpoint=%1 oauth=%2")
                         .arg(this->transport_config.endpoint.toString(),
                              this->transport_config.oauth_enabled ? QStringLiteral("enabled")
                                                                   : QStringLiteral("disabled")));
    if (this->transport_config.oauth_enabled == true)
    {
        emit log_message(QStringLiteral("Initial OAuth state: accessToken=%1 refreshToken=%2 "
                                        "expires=%3 clientIdConfigured=%4")
                             .arg(token_presence_summary(this->access_token),
                                  token_presence_summary(this->refresh_token),
                                  expiry_summary(this->access_token_expiration),
                                  this->registered_client_id.isEmpty() ? QStringLiteral("no")
                                                                       : QStringLiteral("yes")));
    }
    this->set_state(state_t::CONNECTED);
    emit started();
}

void http_transport_t::stop()
{
    if (((this->transport_state == state_t::DISCONNECTED ||
          this->transport_state == state_t::STOPPING) == true))
    {
        return;
    }

    this->set_state(state_t::STOPPING);
    emit log_message(QStringLiteral("Stopping HTTP transport: %1 active request(s)")
                         .arg(this->reply_states.size()));

    const auto replies = this->reply_states.keys();
    for (QNetworkReply *reply : replies)
    {
        reply->abort();
    }
    this->reply_states.clear();
    this->session_id.clear();
    this->authorization_required = false;

    this->set_state(state_t::DISCONNECTED);
    emit stopped();
}

bool http_transport_t::send_message(const QJsonObject &message)
{
    if (((this->transport_state != state_t::CONNECTED) == true))
    {
        emit error_occurred(QStringLiteral(
            "Cannot send an MCP HTTP message while the transport is disconnected."));
        return false;
    }

    this->authorization_required = false;
    if (this->transport_config.oauth_enabled == true)
    {
        emit log_message(
            QStringLiteral("Preparing HTTP MCP request: accessToken=%1 refreshToken=%2 expires=%3")
                .arg(token_presence_summary(this->access_token),
                     token_presence_summary(this->refresh_token),
                     expiry_summary(this->access_token_expiration)));
    }

    if (((this->transport_config.oauth_enabled && !this->refresh_token.isEmpty() &&
          this->access_token_expiration.isValid() &&
          QDateTime::currentDateTimeUtc() >= this->access_token_expiration.addSecs(-30)) == true))
    {
        emit log_message(QStringLiteral(
            "OAuth access token is near expiry; attempting refresh before request."));
        refresh_access_token();
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return this->post_payload(payload, false);
}

QNetworkRequest http_transport_t::build_post_request() const
{
    QNetworkRequest request(this->transport_config.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("MCP-Protocol-Version", this->transport_config.protocol_version.toUtf8());
    if (((this->transport_config.request_timeout_ms > 0) == true))
    {
        request.setTransferTimeout(this->transport_config.request_timeout_ms);
    }

    this->apply_configured_headers(request);
    this->apply_session_header(request);
    this->apply_authorization_header(request);
    return request;
}

void http_transport_t::apply_configured_headers(QNetworkRequest &request) const
{
    for (auto it = this->transport_config.headers.begin();
         ((it != this->transport_config.headers.end()) == true); ++it)
    {
        if (((this->transport_config.oauth_enabled &&
              it.key().compare(QStringLiteral("Authorization"), Qt::CaseInsensitive) == 0) ==
             true))
        {
            continue;
        }
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
}

void http_transport_t::apply_session_header(QNetworkRequest &request) const
{
    if (((!this->session_id.isEmpty()) == true))
    {
        request.setRawHeader("Mcp-Session-Id", this->session_id.toUtf8());
    }
}

void http_transport_t::apply_authorization_header(QNetworkRequest &request) const
{
    if (((this->transport_config.oauth_enabled && !this->access_token.isEmpty()) == true))
    {
        emit const_cast<http_transport_t *>(this)->log_message(
            QStringLiteral("Applying OAuth Authorization header: token=%1 expires=%2")
                .arg(token_presence_summary(this->access_token),
                     expiry_summary(this->access_token_expiration)));
        request.setRawHeader("Authorization",
                             QStringLiteral("Bearer %1").arg(this->access_token).toUtf8());
    }
    else if (this->transport_config.oauth_enabled == true)
    {
        emit const_cast<http_transport_t *>(this)->log_message(
            QStringLiteral("Skipping OAuth Authorization header: accessToken=%1 expires=%2")
                .arg(token_presence_summary(this->access_token),
                     expiry_summary(this->access_token_expiration)));
    }
}

bool http_transport_t::post_payload(const QByteArray &payload, bool oauth_retried)
{
    const QNetworkRequest request = this->build_post_request();
    if (this->transport_config.oauth_enabled == true)
    {
        emit log_message(
            QStringLiteral(
                "Posting HTTP payload: oauthRetry=%1 accessToken=%2 refreshToken=%3 sessionId=%4")
                .arg(oauth_retried ? QStringLiteral("yes") : QStringLiteral("no"),
                     token_presence_summary(this->access_token),
                     token_presence_summary(this->refresh_token),
                     this->session_id.isEmpty() ? QStringLiteral("absent")
                                                : QStringLiteral("present")));
    }
    emit log_message(QStringLiteral("http -> %1").arg(format_payload_for_log(payload)));

    QNetworkReply *reply = this->network_access_manager->post(request, payload);
    this->attach_reply(reply, payload, oauth_retried);
    return true;
}

void http_transport_t::set_state(state_t state)
{
    if (((this->transport_state == state) == true))
    {
        return;
    }

    this->transport_state = state;
    QString state_label = QStringLiteral("unknown");
    switch (this->transport_state)
    {
        case state_t::DISCONNECTED:
            state_label = QStringLiteral("disconnected");
            break;
        case state_t::STARTING:
            state_label = QStringLiteral("starting");
            break;
        case state_t::CONNECTED:
            state_label = QStringLiteral("connected");
            break;
        case state_t::STOPPING:
            state_label = QStringLiteral("stopping");
            break;
    }
    emit log_message(QStringLiteral("http state changed: %1").arg(state_label));
    emit state_changed(this->transport_state);
}

void http_transport_t::attach_reply(QNetworkReply *reply, const QByteArray &payload,
                                    bool oauth_retried)
{
    ReplyState state;
    state.request_payload = payload;
    state.oauth_retried = oauth_retried;
    this->reply_states.insert(reply, state);

    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply]() { this->process_reply_ready_read(reply); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { this->process_reply_finished(reply); });
    connect(reply, &QNetworkReply::errorOccurred, this,
            [this, reply](QNetworkReply::NetworkError code) {
                emit log_message(QStringLiteral("HTTP reply error: code=%1 text=%2")
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
            emit log_message(
                QStringLiteral("HTTP SSL errors: %1").arg(parts.join(QStringLiteral("; "))));
        }
    });
}

void http_transport_t::process_reply_ready_read(QNetworkReply *reply)
{
    auto it = this->reply_states.find(reply);
    if (((it == this->reply_states.end()) == true))
    {
        return;
    }

    this->apply_session_id(reply);
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty() == true)
    {
        return;
    }

    it->buffer += chunk;
    emit log_message(QStringLiteral("http reply bytes: %1").arg(chunk.size()));

    const QString content_type = normalized_content_type(reply);
    if (((content_type == QStringLiteral("text/event-stream")) == true))
    {
        it->is_sse = true;
        this->process_sse_buffer(reply, false);
    }
}

void http_transport_t::process_reply_finished(QNetworkReply *reply)
{
    auto it = this->reply_states.find(reply);
    if (((it == this->reply_states.end()) == true))
    {
        reply->deleteLater();
        return;
    }

    this->apply_session_id(reply);

    const int status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString content_type = normalized_content_type(reply);
    emit log_message(QStringLiteral("HTTP reply finished: status=%1 type=%2")
                         .arg(status_code)
                         .arg(content_type.isEmpty() ? QStringLiteral("(none)") : content_type));

    if (((status_code == 401 && it->emitted_messages == 0 &&
          this->transport_config.oauth_enabled && !it->oauth_retried) == true))
    {
        this->authorization_required = true;
        const QByteArray retry_payload = it->request_payload;
        if (((!it->buffer.isEmpty()) == true))
        {
            emit log_message(QStringLiteral("http <- %1").arg(format_payload_for_log(it->buffer)));
        }
        const QMap<QString, QString> challenge =
            parse_bearer_challenge_parameters(reply->rawHeader("WWW-Authenticate"));
        if (((!challenge.isEmpty()) == true))
        {
            const QString resource_metadata =
                challenge.value(QStringLiteral("resource_metadata")).trimmed();
            const QString error_description =
                challenge.value(QStringLiteral("error_description")).trimmed();
            const QString error_code = challenge.value(QStringLiteral("error")).trimmed();
            if (resource_metadata.isEmpty() == false)
            {
                this->oauth_resource_metadata_url = QUrl(resource_metadata);
                emit log_message(
                    QStringLiteral("OAuth challenge advertised protected-resource metadata: %1")
                        .arg(this->oauth_resource_metadata_url.toString()));
            }
            if (error_code.isEmpty() == false)
            {
                this->last_o_auth_error_message =
                    error_description.isEmpty()
                        ? QStringLiteral("OAuth challenge error: %1").arg(error_code)
                        : QStringLiteral("OAuth challenge error: %1 (%2)")
                              .arg(error_code, error_description);
            }
            if (error_description.isEmpty() == false)
            {
                emit log_message(QStringLiteral("OAuth challenge error description: %1")
                                     .arg(error_description));
            }
        }
        else if (((!it->buffer.trimmed().isEmpty()) == true))
        {
            this->last_o_auth_error_message = QStringLiteral("OAuth authorization required: %1")
                                                  .arg(QString::fromUtf8(it->buffer).trimmed());
        }
        this->access_token.clear();
        this->access_token_expiration = {};
        emit log_message(QStringLiteral("Cleared cached access token after 401; refreshToken=%1")
                             .arg(token_presence_summary(this->refresh_token)));
        const bool authorized = this->ensure_authorized();
        this->reply_states.erase(it);
        reply->deleteLater();
        if (authorized == false)
        {
            emit error_occurred(this->last_o_auth_error_message.isEmpty()
                                    ? QStringLiteral("MCP OAuth authorization failed.")
                                    : this->last_o_auth_error_message);
            return;
        }

        emit log_message(QStringLiteral("Retrying MCP HTTP request after OAuth authorization."));
        this->post_payload(retry_payload, true);
        return;
    }

    if (it->is_sse)
    {
        this->process_sse_buffer(reply, true);
    }
    else if (((!it->buffer.isEmpty()) == true))
    {
        emit log_message(QStringLiteral("http <- %1").arg(format_payload_for_log(it->buffer)));
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(it->buffer, &parse_error);
        if (((parse_error.error != QJsonParseError::NoError) == true))
        {
            emit error_occurred(QStringLiteral("Failed to parse MCP HTTP JSON message: %1")
                                    .arg(parse_error.errorString()));
        }
        else
        {
            this->emit_json_document(reply, document, QStringLiteral("http response"));
        }
    }

    if (((status_code == 404 && !this->session_id.isEmpty()) == true))
    {
        emit log_message(QStringLiteral("HTTP MCP session expired; clearing stored session id."));
        this->session_id.clear();
    }

    if (reply->error() != QNetworkReply::NoError && it->emitted_messages == 0)
    {
        emit error_occurred(QStringLiteral("MCP HTTP transport error (%1): %2")
                                .arg(status_code)
                                .arg(reply->errorString()));
    }

    if (status_code == 202 && it->emitted_messages == 0)
    {
        emit log_message(QStringLiteral("HTTP request accepted with 202 and no response body."));
    }
    else if (status_code != 202 && it->emitted_messages == 0 &&
             reply->error() == QNetworkReply::NoError)
    {
        emit error_occurred(
            QStringLiteral("MCP HTTP reply completed without a JSON-RPC payload."));
    }

    this->reply_states.erase(it);
    reply->deleteLater();
}

void http_transport_t::process_sse_buffer(QNetworkReply *reply, bool flush)
{
    while (true == true)
    {
        auto it = this->reply_states.find(reply);
        if (((it == this->reply_states.end()) == true))
        {
            return;
        }

        const qsizetype newline_index = it->buffer.indexOf('\n');
        if (newline_index < 0)
        {
            break;
        }

        QByteArray line = it->buffer.left(newline_index);
        it->buffer.remove(0, newline_index + 1);
        if (line.endsWith('\r') == true)
        {
            line.chop(1);
        }

        if (line.isEmpty() == true)
        {
            this->handle_sse_event(reply);
            continue;
        }

        if (line.startsWith(':') == true)
        {
            continue;
        }

        if (line.startsWith("event:") == true)
        {
            it->current_event_type = QString::fromUtf8(line.mid(6)).trimmed();
            continue;
        }

        if (line.startsWith("data:") == true)
        {
            QByteArray data = line.mid(5);
            if (data.startsWith(' ') == true)
            {
                data.remove(0, 1);
            }
            if (((!it->current_event_data.isEmpty()) == true))
            {
                it->current_event_data.append('\n');
            }
            it->current_event_data.append(data);
        }
    }

    auto it = this->reply_states.find(reply);
    if (((flush && it != this->reply_states.end() &&
          (!it->current_event_data.isEmpty() || !it->current_event_type.isEmpty())) == true))
    {
        this->handle_sse_event(reply);
    }
}

void http_transport_t::handle_sse_event(QNetworkReply *reply)
{
    auto it = this->reply_states.find(reply);
    if (((it == this->reply_states.end()) == true))
    {
        return;
    }

    const QByteArray data = it->current_event_data;
    const QString event_type = it->current_event_type;
    it->current_event_data.clear();
    it->current_event_type.clear();

    if (((data.trimmed().isEmpty()) == true))
    {
        return;
    }

    emit log_message(QStringLiteral("sse event `%1` <- %2")
                         .arg(event_type.isEmpty() ? QStringLiteral("message") : event_type,
                              format_payload_for_log(data)));

    if (((event_type == QStringLiteral("endpoint")) == true))
    {
        emit log_message(QStringLiteral("Received legacy endpoint event: %1")
                             .arg(QString::fromUtf8(data).trimmed()));
        return;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
    if (((parse_error.error != QJsonParseError::NoError) == true))
    {
        emit error_occurred(QStringLiteral("Failed to parse MCP SSE event JSON: %1")
                                .arg(parse_error.errorString()));
        return;
    }

    this->emit_json_document(reply, document, QStringLiteral("http sse"));
}

void http_transport_t::emit_json_document(QNetworkReply *reply, const QJsonDocument &document,
                                          const QString &source)
{
    auto it = this->reply_states.find(reply);
    if (((it == this->reply_states.end()) == true))
    {
        return;
    }

    if (document.isObject() == true)
    {
        ++it->emitted_messages;
        emit message_received(document.object());
        return;
    }

    if (document.isArray() == true)
    {
        const QJsonArray messages = document.array();
        for (const QJsonValue &message_value : messages)
        {
            if (message_value.isObject() == false)
            {
                emit error_occurred(
                    QStringLiteral("Received a non-object JSON-RPC batch item from %1.")
                        .arg(source));
                continue;
            }
            ++it->emitted_messages;
            emit message_received(message_value.toObject());
        }
        return;
    }

    emit error_occurred(
        QStringLiteral("Received an unsupported JSON payload from %1.").arg(source));
}

void http_transport_t::apply_session_id(QNetworkReply *reply)
{
    const QByteArray header = reply->rawHeader("Mcp-Session-Id");
    if (header.isEmpty() == true)
    {
        return;
    }

    const QString session_id = QString::fromUtf8(header).trimmed();
    if (((session_id.isEmpty() || session_id == this->session_id) == true))
    {
        return;
    }

    this->session_id = session_id;
    emit log_message(QStringLiteral("Stored MCP session id: %1").arg(this->session_id));
}

bool http_transport_t::ensure_authorized()
{
    if (((!this->transport_config.oauth_enabled) == true))
    {
        return false;
    }

    this->last_o_auth_error_message.clear();
    emit log_message(
        QStringLiteral("Ensuring OAuth authorization: accessToken=%1 refreshToken=%2 expires=%3 "
                       "authorizationUrl=%4 tokenUrl=%5")
            .arg(token_presence_summary(this->access_token),
                 token_presence_summary(this->refresh_token),
                 expiry_summary(this->access_token_expiration),
                 this->authorization_url.isEmpty() ? QStringLiteral("(unset)")
                                                   : this->authorization_url.toString(),
                 this->token_url.isEmpty() ? QStringLiteral("(unset)")
                                           : this->token_url.toString()));

#if !QTMCP_HAS_NETWORKAUTH
    this->lastOAuthErrorMessage =
        QStringLiteral("MCP OAuth requires Qt NetworkAuth support in qtmcp.");
    emit error_occurred(this->lastOAuthErrorMessage);
    return false;
#else
    if (((!this->refresh_token.isEmpty()) == true))
    {
        emit log_message(QStringLiteral("Attempting OAuth refresh token flow."));
        if (this->refresh_access_token() == true)
        {
            return true;
        }
        emit log_message(QStringLiteral(
            "OAuth refresh failed; falling back to interactive authorization code flow."));
    }

    if (((!this->transport_config.interactive_o_auth_enabled) == true))
    {
        if (this->last_o_auth_error_message.isEmpty() == true)
        {
            this->last_o_auth_error_message =
                this->authorization_required
                    ? QStringLiteral("OAuth authorization required.")
                    : QStringLiteral("Interactive OAuth authorization is disabled.");
        }
        emit log_message(
            QStringLiteral("Interactive OAuth authorization is disabled for this flow."));
        return false;
    }

    return this->run_authorization_code_grant();
#endif
}

bool http_transport_t::refresh_access_token()
{
#if !QTMCP_HAS_NETWORKAUTH
    return false;
#else
    if (((this->registered_client_id.isEmpty() || !this->token_url.isValid() ||
          this->refresh_token.isEmpty()) == true))
    {
        this->last_o_auth_error_message =
            QStringLiteral("OAuth refresh cannot start because client "
                           "registration, token URL, or refresh token is missing.");
        return false;
    }

    emit log_message(
        QStringLiteral("Starting OAuth refresh: clientIdConfigured=%1 refreshToken=%2 tokenUrl=%3")
            .arg(this->registered_client_id.isEmpty() ? QStringLiteral("no")
                                                      : QStringLiteral("yes"),
                 token_presence_summary(this->refresh_token), this->token_url.toString()));
    QOAuth2AuthorizationCodeFlow oauth(this->network_access_manager);
    oauth.setClientIdentifier(this->registered_client_id);
    if (((!this->registered_client_secret.isEmpty()) == true))
    {
        oauth.setClientIdentifierSharedKey(this->registered_client_secret);
    }
    oauth.setTokenUrl(this->token_url);
    if (this->authorization_url.isValid() == true)
    {
        oauth.setAuthorizationUrl(this->authorization_url);
    }
    oauth.setRefreshToken(this->refresh_token);
    if (((!this->access_token.isEmpty()) == true))
    {
        oauth.setToken(this->access_token);
    }
    if (((!this->transport_config.oauth_scopes.isEmpty()) == true))
    {
        oauth.setRequestedScopeTokens(requested_scope_tokens(this->transport_config.oauth_scopes));
    }
    oauth.setModifyParametersFunction(
        [this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            this->apply_o_auth_parameters(stage, parameters);
        });
    oauth.setNetworkRequestModifier(this, [this](QNetworkRequest &request, QAbstractOAuth::Stage) {
        if (((this->transport_config.request_timeout_ms > 0) == true))
        {
            request.setTransferTimeout(this->transport_config.request_timeout_ms);
        }
    });

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QString failure;
    bool success = false;

    QObject::connect(&oauth, &QAbstractOAuth::tokenChanged, &loop, [&](const QString &token) {
        emit log_message(
            QStringLiteral("OAuth refresh tokenChanged: %1").arg(token_presence_summary(token)));
        if (token.isEmpty() == false)
        {
            success = true;
            loop.quit();
        }
    });
    QObject::connect(&oauth, &QAbstractOAuth::granted, &loop, [&]() {
        emit log_message(
            QStringLiteral("OAuth refresh granted signal: token=%1 refreshToken=%2 expires=%3")
                .arg(token_presence_summary(oauth.token()),
                     token_presence_summary(oauth.refreshToken()),
                     expiry_summary(oauth.expirationAt())));
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

    timer.start(this->transport_config.request_timeout_ms > 0
                    ? this->transport_config.request_timeout_ms
                    : k_o_auth_interactive_timeout_ms);
    oauth.refreshTokens();
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (success == false)
    {
        this->last_o_auth_error_message =
            failure.isEmpty() ? QStringLiteral("OAuth refresh timed out.") : failure;
        emit log_message(this->last_o_auth_error_message);
        return false;
    }

    this->access_token = oauth.token();
    if (((!oauth.refreshToken().isEmpty()) == true))
    {
        this->refresh_token = oauth.refreshToken();
    }
    this->access_token_expiration = oauth.expirationAt();
    if (this->access_token.isEmpty() == true)
    {
        this->last_o_auth_error_message =
            QStringLiteral("OAuth refresh finished without returning an access token.");
        emit log_message(this->last_o_auth_error_message);
        return false;
    }
    emit log_message(
        QStringLiteral("OAuth refresh result: accessToken=%1 refreshToken=%2 expires=%3")
            .arg(token_presence_summary(this->access_token),
                 token_presence_summary(this->refresh_token),
                 expiry_summary(this->access_token_expiration)));
    this->save_cached_o_auth_state();
    emit log_message(QStringLiteral("OAuth refresh succeeded."));
    return true;
#endif
}

bool http_transport_t::run_authorization_code_grant()
{
#if !QTMCP_HAS_NETWORKAUTH
    return false;
#else
    QOAuthHttpServerReplyHandler reply_handler;
    reply_handler.setCallbackPath(QStringLiteral("/oauth2/callback"));
    reply_handler.setCallbackText(
        QStringLiteral("Authorization completed. You can return to Qt Creator."));
    QString listen_details;
    if (start_o_auth_callback_listener(&reply_handler, &listen_details) == false)
    {
        this->last_o_auth_error_message =
            listen_details.isEmpty()
                ? QStringLiteral("Failed to start local OAuth callback listener.")
                : listen_details;
        emit error_occurred(this->last_o_auth_error_message);
        return false;
    }
    if (listen_details.isEmpty() == false)
    {
        emit log_message(listen_details);
    }
    emit log_message(
        QStringLiteral("OAuth callback configuration: callback=%1").arg(reply_handler.callback()));

    o_auth_endpoints_t endpoints;
    QString discovery_log;
    protected_resource_metadata_t protected_resource;
    QString protected_resource_log;
    const QUrl protected_resource_metadata_url =
        this->oauth_resource_metadata_url.isValid() && !this->oauth_resource_metadata_url.isEmpty()
            ? this->oauth_resource_metadata_url
            : default_protected_resource_metadata_url(this->transport_config.endpoint);
    const bool have_protected_resource_metadata = load_protected_resource_metadata(
        this->network_access_manager, this->transport_config, protected_resource_metadata_url,
        &protected_resource, &protected_resource_log);
    if (protected_resource_log.isEmpty() == false)
    {
        emit log_message(protected_resource_log);
    }

    if (have_protected_resource_metadata && !protected_resource.resource.trimmed().isEmpty())
    {
        this->oauth_resource = protected_resource.resource.trimmed();
    }

    const QUrl authorization_metadata_url =
        have_protected_resource_metadata && !protected_resource.authorization_servers.isEmpty()
            ? oauth_authorization_metadata_url(
                  protected_resource.authorization_servers.constFirst())
            : oauth_authorization_metadata_url(
                  authorization_base_url(this->transport_config.endpoint));

    if (((!discover_o_auth_endpoints(this->network_access_manager, this->transport_config,
                                     authorization_metadata_url, &endpoints, &discovery_log)) ==
         true))
    {
        this->last_o_auth_error_message = QStringLiteral("Failed to resolve OAuth endpoints.");
        emit error_occurred(this->last_o_auth_error_message);
        return false;
    }
    if (discovery_log.isEmpty() == false)
    {
        emit log_message(discovery_log);
    }

    this->authorization_url = endpoints.authorization_endpoint;
    this->token_url = endpoints.token_endpoint;
    emit log_message(
        QStringLiteral(
            "Resolved OAuth endpoints: authorization=%1 token=%2 registration=%3 resource=%4")
            .arg(this->authorization_url.toString(), this->token_url.toString(),
                 endpoints.registration_endpoint.isEmpty()
                     ? QStringLiteral("(unset)")
                     : endpoints.registration_endpoint.toString(),
                 this->oauth_resource.isEmpty() ? QStringLiteral("(unset)")
                                                : this->oauth_resource));

    if (((!this->transport_config.oauth_client_id.isEmpty()) == true))
    {
        this->registered_client_id = this->transport_config.oauth_client_id;
    }
    if (((!this->transport_config.oauth_client_secret.isEmpty()) == true))
    {
        this->registered_client_secret = this->transport_config.oauth_client_secret;
    }

    if (this->registered_client_id.isEmpty() == true)
    {
        if (endpoints.registration_endpoint.isValid() == false)
        {
            this->last_o_auth_error_message =
                QStringLiteral("OAuth server does not expose dynamic client "
                               "registration and no oauthClientId was configured.");
            emit error_occurred(this->last_o_auth_error_message);
            return false;
        }

        registered_o_auth_client_t registered_client;
        QString registration_error;
        if (((!register_o_auth_client(this->network_access_manager, this->transport_config,
                                      endpoints.registration_endpoint, reply_handler.callback(),
                                      &registered_client, &registration_error)) == true))
        {
            this->last_o_auth_error_message = registration_error;
            emit error_occurred(this->last_o_auth_error_message);
            return false;
        }

        this->registered_client_id = registered_client.client_id;
        this->registered_client_secret = registered_client.client_secret;
        emit log_message(QStringLiteral("Registered OAuth client dynamically."));
    }

    QOAuth2AuthorizationCodeFlow oauth(this->network_access_manager);
    oauth.setReplyHandler(&reply_handler);
    oauth.setAuthorizationUrl(this->authorization_url);
    oauth.setTokenUrl(this->token_url);
    oauth.setClientIdentifier(this->registered_client_id);
    if (((!this->registered_client_secret.isEmpty()) == true))
    {
        oauth.setClientIdentifierSharedKey(this->registered_client_secret);
    }
    oauth.setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    if (((!this->transport_config.oauth_scopes.isEmpty()) == true))
    {
        oauth.setRequestedScopeTokens(requested_scope_tokens(this->transport_config.oauth_scopes));
    }
    oauth.setModifyParametersFunction(
        [this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            this->apply_o_auth_parameters(stage, parameters);
        });
    oauth.setNetworkRequestModifier(this, [this](QNetworkRequest &request, QAbstractOAuth::Stage) {
        if (((this->transport_config.request_timeout_ms > 0) == true))
        {
            request.setTransferTimeout(this->transport_config.request_timeout_ms);
        }
    });

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QString failure;
    bool success = false;

    QObject::connect(&oauth, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
        emit log_message(
            QStringLiteral("Opening browser for OAuth authorization: %1").arg(url.toString()));
        if (QDesktopServices::openUrl(url) == false)
        {
            emit log_message(
                QStringLiteral("Failed to open browser automatically. Open this URL manually: %1")
                    .arg(url.toString()));
        }
    });
    QObject::connect(&oauth, &QAbstractOAuth::tokenChanged, &loop, [&](const QString &token) {
        emit log_message(QStringLiteral("OAuth authorization tokenChanged: %1")
                             .arg(token_presence_summary(token)));
        if (token.isEmpty() == false)
        {
            success = true;
            loop.quit();
        }
    });
    QObject::connect(&oauth, &QAbstractOAuth::granted, &loop, [&]() {
        emit log_message(
            QStringLiteral(
                "OAuth authorization granted signal: token=%1 refreshToken=%2 expires=%3")
                .arg(token_presence_summary(oauth.token()),
                     token_presence_summary(oauth.refreshToken()),
                     expiry_summary(oauth.expirationAt())));
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

    timer.start(k_o_auth_interactive_timeout_ms);
    emit log_message(
        QStringLiteral("Starting OAuth authorization code flow: clientIdConfigured=%1 "
                       "clientSecretConfigured=%2 scopes=%3")
            .arg(this->registered_client_id.isEmpty() ? QStringLiteral("no")
                                                      : QStringLiteral("yes"),
                 this->registered_client_secret.isEmpty() ? QStringLiteral("no")
                                                          : QStringLiteral("yes"),
                 this->transport_config.oauth_scopes.join(QStringLiteral(","))));
    oauth.grant();
    loop.exec();

    if (success == false)
    {
        this->last_o_auth_error_message =
            failure.isEmpty() ? QStringLiteral("OAuth authorization timed out.") : failure;
        emit error_occurred(this->last_o_auth_error_message);
        return false;
    }

    this->access_token = oauth.token();
    this->refresh_token = oauth.refreshToken();
    this->access_token_expiration = oauth.expirationAt();
    if (this->access_token.isEmpty() == true)
    {
        this->last_o_auth_error_message =
            QStringLiteral("OAuth authorization finished without returning an access token.");
        emit error_occurred(this->last_o_auth_error_message);
        return false;
    }
    emit log_message(
        QStringLiteral("OAuth authorization result: accessToken=%1 refreshToken=%2 expires=%3")
            .arg(token_presence_summary(this->access_token),
                 token_presence_summary(this->refresh_token),
                 expiry_summary(this->access_token_expiration)));
    this->save_cached_o_auth_state();
    emit log_message(QStringLiteral("OAuth authorization succeeded."));
    return true;
#endif
}

#if QTMCP_HAS_NETWORKAUTH
void http_transport_t::apply_o_auth_parameters(QAbstractOAuth::Stage stage,
                                               QMultiMap<QString, QVariant> *parameters) const
{
    Q_UNUSED(stage);
    if (((parameters == nullptr || this->oauth_resource.trimmed().isEmpty()) == true))
    {
        return;
    }

    parameters->insert(QStringLiteral("resource"), this->oauth_resource.trimmed());
}
#endif

void http_transport_t::load_cached_o_auth_state()
{
    if (((!this->transport_config.oauth_enabled) == true))
    {
        return;
    }

    const QString cache_key = oauth_cache_key(this->transport_config);
    emit log_message(
        QStringLiteral("Looking up OAuth cache: endpoint=%1 clientIdConfigured=%2 scopes=%3")
            .arg(this->transport_config.endpoint.toString(),
                 this->transport_config.oauth_client_id.isEmpty() ? QStringLiteral("no")
                                                                  : QStringLiteral("yes"),
                 this->transport_config.oauth_scopes.join(QStringLiteral(","))));
    auto it = oauth_cache().constFind(cache_key);
    if (it == oauth_cache().cend())
    {
        o_auth_cache_entry_t persisted_entry;
        QString persistent_error;
        if (load_persistent_o_auth_cache_entry(cache_key, &persisted_entry, &persistent_error) ==
            true)
        {
            oauth_cache().insert(cache_key, persisted_entry);
            it = oauth_cache().constFind(cache_key);
            emit log_message(QStringLiteral("Loaded OAuth state from persistent cache file: %1")
                                 .arg(persistent_o_auth_cache_path()));
        }
        else
        {
            if (persistent_error.isEmpty() == false)
            {
                emit log_message(QStringLiteral("Persistent OAuth cache read failed: %1")
                                     .arg(persistent_error));
            }
            if (((!this->transport_config.oauth_client_id.isEmpty()) == true))
            {
                this->registered_client_id = this->transport_config.oauth_client_id;
            }
            if (((!this->transport_config.oauth_client_secret.isEmpty()) == true))
            {
                this->registered_client_secret = this->transport_config.oauth_client_secret;
            }
            emit log_message(QStringLiteral("OAuth cache miss."));
            return;
        }
    }

    this->access_token = it->access_token;
    this->refresh_token = it->refresh_token;
    this->access_token_expiration = it->expiration;
    this->authorization_url = it->authorization_url;
    this->token_url = it->token_url;
    this->registered_client_id = this->transport_config.oauth_client_id.isEmpty()
                                     ? it->client_id
                                     : this->transport_config.oauth_client_id;
    this->registered_client_secret = this->transport_config.oauth_client_secret.isEmpty()
                                         ? it->client_secret
                                         : this->transport_config.oauth_client_secret;
    this->oauth_resource = it->resource;
    this->oauth_resource_metadata_url = it->resource_metadata_url;

    emit log_message(
        QStringLiteral(
            "Loaded cached OAuth state for %1: accessToken=%2 refreshToken=%3 expires=%4")
            .arg(this->transport_config.endpoint.toString(),
                 token_presence_summary(this->access_token),
                 token_presence_summary(this->refresh_token),
                 expiry_summary(this->access_token_expiration)));
}

void http_transport_t::save_cached_o_auth_state() const
{
    if (((!this->transport_config.oauth_enabled) == true))
    {
        return;
    }

    o_auth_cache_entry_t entry;
    entry.access_token = this->access_token;
    entry.refresh_token = this->refresh_token;
    entry.expiration = this->access_token_expiration;
    entry.authorization_url = this->authorization_url;
    entry.token_url = this->token_url;
    entry.client_id = this->registered_client_id;
    entry.client_secret = this->registered_client_secret;
    entry.resource = this->oauth_resource;
    entry.resource_metadata_url = this->oauth_resource_metadata_url;
    oauth_cache().insert(oauth_cache_key(this->transport_config), entry);
    emit const_cast<http_transport_t *>(this)->log_message(
        QStringLiteral(
            "Saved OAuth state to cache for %1: accessToken=%2 refreshToken=%3 expires=%4")
            .arg(this->transport_config.endpoint.toString(),
                 token_presence_summary(this->access_token),
                 token_presence_summary(this->refresh_token),
                 expiry_summary(this->access_token_expiration)));
    QString persistent_error;
    if (((!save_persistent_o_auth_cache_entry(oauth_cache_key(this->transport_config), entry,
                                              &persistent_error)) == true))
    {
        emit const_cast<http_transport_t *>(this)->log_message(
            QStringLiteral("Failed to persist OAuth cache: %1").arg(persistent_error));
    }
    else
    {
        emit const_cast<http_transport_t *>(this)->log_message(
            QStringLiteral("Saved OAuth state to persistent cache file: %1")
                .arg(persistent_o_auth_cache_path()));
    }
}

}  // namespace qtmcp
