/*! Declares the streamable HTTP transport implementation for the Qt MCP client. */
#pragma once

#include <qtmcp/Transport.h>

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QMap>
#include <QMultiMap>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#if defined(QTMCP_HAS_NETWORKAUTH) && QTMCP_HAS_NETWORKAUTH
#include <QAbstractOAuth>
#endif

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace qtmcp
{

struct http_transport_config_t
{
    QUrl endpoint;
    QMap<QString, QString> headers;
    QString protocol_version = QStringLiteral("2025-03-26");
    int request_timeout_ms = 30000;
    bool oauth_enabled = false;
    bool interactive_o_auth_enabled = true;
    QString oauth_client_id;
    QString oauth_client_secret;
    QStringList oauth_scopes;
    QString oauth_client_name = QStringLiteral("qcai2");
};

class http_transport_t final : public transport_t
{
    Q_OBJECT

public:
    explicit http_transport_t(http_transport_config_t config, QObject *parent = nullptr);
    ~http_transport_t() override;

    QString transport_name() const override;
    state_t state() const override;
    const http_transport_config_t &config() const;
    bool authorize(QString *error_message = nullptr);
    QString last_o_auth_error() const;
    bool last_authorization_required() const;
    bool has_cached_o_auth_credentials() const;

    void start() override;
    void stop() override;
    bool send_message(const QJsonObject &message) override;

private:
    struct ReplyState;

    QNetworkRequest build_post_request() const;
    void apply_configured_headers(QNetworkRequest &request) const;
    void apply_session_header(QNetworkRequest &request) const;
    void apply_authorization_header(QNetworkRequest &request) const;
    bool post_payload(const QByteArray &payload, bool oauth_retried);
    void set_state(state_t state);
    void attach_reply(QNetworkReply *reply, const QByteArray &payload, bool oauth_retried);
    void process_reply_ready_read(QNetworkReply *reply);
    void process_reply_finished(QNetworkReply *reply);
    void process_sse_buffer(QNetworkReply *reply, bool flush);
    void handle_sse_event(QNetworkReply *reply);
    void emit_json_document(QNetworkReply *reply, const QJsonDocument &document,
                            const QString &source);
    void apply_session_id(QNetworkReply *reply);
    bool ensure_authorized();
    bool refresh_access_token();
    bool run_authorization_code_grant();
#if defined(QTMCP_HAS_NETWORKAUTH) && QTMCP_HAS_NETWORKAUTH
    void apply_o_auth_parameters(QAbstractOAuth::Stage stage,
                                 QMultiMap<QString, QVariant> *parameters) const;
#endif
    void load_cached_o_auth_state();
    void save_cached_o_auth_state() const;

    http_transport_config_t transport_config;
    QNetworkAccessManager *network_access_manager = nullptr;
    QHash<QNetworkReply *, ReplyState> reply_states;
    QString session_id;
    QString access_token;
    QString refresh_token;
    QDateTime access_token_expiration;
    QUrl authorization_url;
    QUrl token_url;
    QString registered_client_id;
    QString registered_client_secret;
    QString oauth_resource;
    QUrl oauth_resource_metadata_url;
    QString last_o_auth_error_message;
    bool authorization_required = false;
    state_t transport_state = state_t::DISCONNECTED;
};

}  // namespace qtmcp
