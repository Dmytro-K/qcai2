/*! Declares the settings widget used to edit global MCP server definitions. */
#pragma once

#include <qtmcp/ServerDefinition.h>

#include <QSet>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;

namespace qtmcp
{
class Client;
}

namespace qcai2
{

class McpServersWidget final : public QWidget
{
    Q_OBJECT

public:
    enum class HttpStatusKind
    {
        Unknown,
        Checking,
        Reachable,
        AuthorizationRequired,
        Error
    };

    struct HttpStatus
    {
        HttpStatusKind kind = HttpStatusKind::Unknown;
        QString message;
    };

    explicit McpServersWidget(QWidget *parent = nullptr);

    void setServers(const qtmcp::ServerDefinitions &servers);
    qtmcp::ServerDefinitions servers() const;
private:
    QString currentServerName() const;
    qtmcp::ServerDefinition *currentServer();
    const qtmcp::ServerDefinition *currentServer() const;

    void repopulateServerList(const QString &selectedServer = {});
    void loadCurrentServer();
    void clearEditors();
    void setEditorsEnabled(bool enabled);
    void updateTransportPage(const QString &transport);
    void updateCurrentHttpStatusUi();
    void updateServerListItem(const QString &serverName);
    void setHttpStatus(const QString &serverName, HttpStatusKind kind, const QString &message);
    void clearHttpStatus(const QString &serverName);
    void loadPersistedConnectionStates();
    void persistConnectionStates() const;
    void finishConnectionTest(const QString &serverName, HttpStatusKind kind,
                              const QString &message, bool authorized = false);
    void cleanupConnectionTest();
    void invalidateCurrentServerStatus();
    void testCurrentServerConnection();
    void authorizeCurrentServer();
    QString makeUniqueServerName(const QString &baseName) const;
    void renameCurrentServer();

    static QStringList textLines(const QString &text);
    static QStringList joinMapLines(const QMap<QString, QString> &values, QChar separator,
                                    bool insertSpaceAfterSeparator);
    static QMap<QString, QString> parseMapLines(const QString &text, QChar separator);

    qtmcp::ServerDefinitions m_servers;
    bool m_updating = false;

    QListWidget *m_serverList = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    QComboBox *m_transportCombo = nullptr;
    QStackedWidget *m_transportPages = nullptr;
    QLabel *m_unknownTransportLabel = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QPlainTextEdit *m_argsEdit = nullptr;
    QPlainTextEdit *m_envEdit = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QLineEdit *m_tokenEdit = nullptr;
    QPlainTextEdit *m_headersEdit = nullptr;
    QLabel *m_httpStatusLabel = nullptr;
    QPushButton *m_testConnectionButton = nullptr;
    QPushButton *m_authorizeButton = nullptr;
    QSpinBox *m_startupTimeoutSpin = nullptr;
    QSpinBox *m_requestTimeoutSpin = nullptr;
    QTimer *m_connectionTestTimer = nullptr;
    qtmcp::Client *m_connectionTestClient = nullptr;
    QString m_connectionTestServerName;
    qint64 m_connectionTestRequestId = 0;
    QMap<QString, HttpStatus> m_httpStatuses;
    QSet<QString> m_authorizedServers;
    bool m_authorizing = false;
    bool m_testingConnection = false;
};

}  // namespace qcai2
