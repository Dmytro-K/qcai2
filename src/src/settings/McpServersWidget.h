/*! Declares the settings widget used to edit global MCP server definitions. */
#pragma once

#include <qtmcp/ServerDefinition.h>

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

namespace qcai2
{

class McpServersWidget final : public QWidget
{
    Q_OBJECT

public:
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
    QPlainTextEdit *m_headersEdit = nullptr;
    QSpinBox *m_startupTimeoutSpin = nullptr;
    QSpinBox *m_requestTimeoutSpin = nullptr;
};

}  // namespace qcai2
