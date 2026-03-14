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

struct HttpTransportConfig
{
    QUrl endpoint;
    QMap<QString, QString> headers;
    QString protocolVersion = QStringLiteral("2025-03-26");
    int requestTimeoutMs = 30000;
    bool oauthEnabled = false;
    bool interactiveOAuthEnabled = true;
    QString oauthClientId;
    QString oauthClientSecret;
    QStringList oauthScopes;
    QString oauthClientName = QStringLiteral("qcai2");
};

class HttpTransport final : public Transport
{
    Q_OBJECT

public:
    explicit HttpTransport(HttpTransportConfig config, QObject *parent = nullptr);
    ~HttpTransport() override;

    QString transportName() const override;
    State state() const override;
    const HttpTransportConfig &config() const;
    bool authorize(QString *errorMessage = nullptr);
    QString lastOAuthError() const;
    bool lastAuthorizationRequired() const;
    bool hasCachedOAuthCredentials() const;

    void start() override;
    void stop() override;
    bool sendMessage(const QJsonObject &message) override;

private:
    struct ReplyState;

    QNetworkRequest buildPostRequest() const;
    void applyConfiguredHeaders(QNetworkRequest &request) const;
    void applySessionHeader(QNetworkRequest &request) const;
    void applyAuthorizationHeader(QNetworkRequest &request) const;
    bool postPayload(const QByteArray &payload, bool oauthRetried);
    void setState(State state);
    void attachReply(QNetworkReply *reply, const QByteArray &payload, bool oauthRetried);
    void processReplyReadyRead(QNetworkReply *reply);
    void processReplyFinished(QNetworkReply *reply);
    void processSseBuffer(QNetworkReply *reply, bool flush);
    void handleSseEvent(QNetworkReply *reply);
    void emitJsonDocument(QNetworkReply *reply, const QJsonDocument &document, const QString &source);
    void applySessionId(QNetworkReply *reply);
    bool ensureAuthorized();
    bool refreshAccessToken();
    bool runAuthorizationCodeGrant();
#if defined(QTMCP_HAS_NETWORKAUTH) && QTMCP_HAS_NETWORKAUTH
    void applyOAuthParameters(QAbstractOAuth::Stage stage,
                              QMultiMap<QString, QVariant> *parameters) const;
#endif
    void loadCachedOAuthState();
    void saveCachedOAuthState() const;

    HttpTransportConfig m_config;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QHash<QNetworkReply *, ReplyState> m_replyStates;
    QString m_sessionId;
    QString m_accessToken;
    QString m_refreshToken;
    QDateTime m_accessTokenExpiration;
    QUrl m_authorizationUrl;
    QUrl m_tokenUrl;
    QString m_registeredClientId;
    QString m_registeredClientSecret;
    QString m_oauthResource;
    QUrl m_oauthResourceMetadataUrl;
    QString m_lastOAuthError;
    bool m_lastAuthorizationRequired = false;
    State m_state = State::Disconnected;
};

}  // namespace qtmcp
