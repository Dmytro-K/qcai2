/*! Implements the settings widget used to edit global MCP server definitions. */

#include "McpServersWidget.h"

#include <qtmcp/Client.h>
#include <qtmcp/HttpTransport.h>

#include "../util/Logger.h"
#include "../util/Migration.h"
#include "Settings.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace qcai2
{

namespace
{

void logMcpSettingsDebug(const QString &serverName, const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty() == true)
    {
        return;
    }
    QCAI_DEBUG("MCP", QStringLiteral("[settings:%1] %2").arg(serverName, trimmed));
}

constexpr auto kMcpProtocolVersionHttp = "2025-03-26";
constexpr int kStatusProbeTimeoutMs = 10000;

QString expandEnvironmentPlaceholders(const QString &value, const QProcessEnvironment &environment)
{
    QString resolved = value;
    static const QRegularExpression pattern(QStringLiteral(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})"));
    auto match = pattern.match(resolved);
    while (match.hasMatch())
    {
        const QString variableName = match.captured(1);
        const QString replacement = environment.value(variableName);
        resolved.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        match = pattern.match(resolved);
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
    return value.isBool() ? value.toBool() : fallback;
}

QStringList extraFieldStringList(const QJsonObject &extraFields, const QString &name)
{
    QStringList values;
    const QJsonValue value = extraFields.value(name);
    if (value.isString() == true)
    {
        return value.toString().split(QRegularExpression(QStringLiteral("\\s+")),
                                      Qt::SkipEmptyParts);
    }

    if (value.isArray() == false)
    {
        return values;
    }

    for (const QJsonValue &entry : value.toArray())
    {
        if (entry.isString() == false)
        {
            continue;
        }
        const QString trimmed = entry.toString().trimmed();
        if (trimmed.isEmpty() == false)
        {
            values.append(trimmed);
        }
    }
    return values;
}

QString extractBearerToken(QMap<QString, QString> *headers)
{
    if (((headers == nullptr) == true))
    {
        return {};
    }

    const auto keys = headers->keys();
    for (const QString &key : keys)
    {
        if (key.compare(QStringLiteral("Authorization"), Qt::CaseInsensitive) != 0)
        {
            continue;
        }

        const QString value = headers->value(key).trimmed();
        if (!value.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
        {
            return {};
        }

        headers->remove(key);
        return value.mid(7).trimmed();
    }

    return {};
}

void removeAuthorizationHeaders(QMap<QString, QString> *headers)
{
    if (((headers == nullptr) == true))
    {
        return;
    }

    const auto keys = headers->keys();
    for (const QString &key : keys)
    {
        if (key.compare(QStringLiteral("Authorization"), Qt::CaseInsensitive) == 0)
        {
            headers->remove(key);
        }
    }
}

QMap<QString, QString> normalizedHeadersWithToken(const QString &headersText,
                                                  const QString &tokenText)
{
    QMap<QString, QString> headers;
    for (const QString &line : headersText.split(QLatin1Char('\n')))
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }

        const qsizetype separatorIndex = trimmed.indexOf(QLatin1Char(':'));
        if (separatorIndex < 0)
        {
            headers.insert(trimmed, QString());
            continue;
        }

        const QString key = trimmed.left(separatorIndex).trimmed();
        const QString value = trimmed.mid(separatorIndex + 1).trimmed();
        if (!key.isEmpty())
        {
            headers.insert(key, value);
        }
    }

    const QString token = tokenText.trimmed();
    if (token.isEmpty() == false)
    {
        removeAuthorizationHeaders(&headers);
        headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(token));
        return headers;
    }

    QString extractedToken = extractBearerToken(&headers);
    if (extractedToken.isEmpty() == false)
    {
        headers.insert(QStringLiteral("Authorization"),
                       QStringLiteral("Bearer %1").arg(extractedToken));
    }
    return headers;
}

QJsonObject initializeParamsObject()
{
    return QJsonObject{
        {QStringLiteral("protocolVersion"), QString::fromLatin1(kMcpProtocolVersionHttp)},
        {QStringLiteral("capabilities"), QJsonObject{}},
        {QStringLiteral("clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("qcai2")},
                     {QStringLiteral("version"), Migration::currentRevisionString()}}}};
}

int probeTimeoutMs(int configuredTimeoutMs)
{
    if (((configuredTimeoutMs <= 0) == true))
    {
        return kStatusProbeTimeoutMs;
    }
    return std::min(configuredTimeoutMs, kStatusProbeTimeoutMs);
}

qtmcp::HttpTransportConfig buildHttpTransportConfig(const QString &serverName,
                                                    const qtmcp::ServerDefinition &definition)
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    qtmcp::HttpTransportConfig config;
    config.endpoint = QUrl(expandEnvironmentPlaceholders(definition.url, environment));
    config.protocolVersion = QString::fromLatin1(kMcpProtocolVersionHttp);
    config.requestTimeoutMs = definition.requestTimeoutMs;
    config.oauthEnabled = extraFieldBool(definition.extraFields, QStringLiteral("oauthEnabled"),
                                         definition.headers.isEmpty());
    config.oauthClientId =
        extraFieldString(definition.extraFields, QStringLiteral("oauthClientId"));
    config.oauthClientSecret = expandEnvironmentPlaceholders(
        extraFieldString(definition.extraFields, QStringLiteral("oauthClientSecret")),
        environment);
    config.oauthClientName =
        extraFieldString(definition.extraFields, QStringLiteral("oauthClientName"));
    if (config.oauthClientName.isEmpty())
    {
        config.oauthClientName = QStringLiteral("qcai2");
    }
    config.oauthScopes =
        extraFieldStringList(definition.extraFields, QStringLiteral("oauthScopes"));
    for (auto it = definition.headers.begin(); it != definition.headers.end(); ++it)
    {
        config.headers.insert(it.key(), expandEnvironmentPlaceholders(it.value(), environment));
    }

    Q_UNUSED(serverName);
    return config;
}

QColor statusColor(McpServersWidget::HttpStatusKind kind, const QWidget *widget)
{
    switch (kind)
    {
        case McpServersWidget::HttpStatusKind::Reachable:
            return QColor(0, 128, 0);
        case McpServersWidget::HttpStatusKind::AuthorizationRequired:
        case McpServersWidget::HttpStatusKind::Error:
            return QColor(180, 0, 0);
        default:
            return widget->palette().color(QPalette::Text);
    }
}

QString notTestedText()
{
    return McpServersWidget::tr("Not tested");
}

QString checkingText()
{
    return McpServersWidget::tr("Checking...");
}

QString authorizedText()
{
    return McpServersWidget::tr("Authorized");
}

QString connectedText()
{
    return McpServersWidget::tr("Connected");
}

QString authorizationRequiredText()
{
    return McpServersWidget::tr("Authorization required");
}

QString authorizingText()
{
    return McpServersWidget::tr("Authorizing...");
}

QString errorPrefixText()
{
    return McpServersWidget::tr("Error");
}

}  // namespace

McpServersWidget::McpServersWidget(QWidget *parent) : QWidget(parent)
{
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *listColumn = new QVBoxLayout;
    m_serverList = new QListWidget(this);
    listColumn->addWidget(m_serverList, 1);

    auto *buttonRow = new QHBoxLayout;
    m_addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    listColumn->addLayout(buttonRow);

    auto *editorGroup = new QGroupBox(tr("Server"), this);
    auto *editorLayout = new QVBoxLayout(editorGroup);
    auto *formLayout = new QFormLayout;

    m_nameEdit = new QLineEdit(editorGroup);
    m_enabledCheck = new QCheckBox(tr("Enabled"), editorGroup);
    m_transportCombo = new QComboBox(editorGroup);
    m_transportCombo->setEditable(true);
    m_transportCombo->addItems(
        {QStringLiteral("stdio"), QStringLiteral("http"), QStringLiteral("sse")});
    m_startupTimeoutSpin = new QSpinBox(editorGroup);
    m_startupTimeoutSpin->setRange(0, 600000);
    m_startupTimeoutSpin->setSuffix(tr(" ms"));
    m_requestTimeoutSpin = new QSpinBox(editorGroup);
    m_requestTimeoutSpin->setRange(0, 600000);
    m_requestTimeoutSpin->setSuffix(tr(" ms"));

    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(QString(), m_enabledCheck);
    formLayout->addRow(tr("Transport:"), m_transportCombo);
    formLayout->addRow(tr("Startup timeout:"), m_startupTimeoutSpin);
    formLayout->addRow(tr("Request timeout:"), m_requestTimeoutSpin);
    editorLayout->addLayout(formLayout);

    m_transportPages = new QStackedWidget(editorGroup);

    auto *stdioPage = new QWidget(m_transportPages);
    auto *stdioLayout = new QFormLayout(stdioPage);
    m_commandEdit = new QLineEdit(stdioPage);
    m_argsEdit = new QPlainTextEdit(stdioPage);
    m_argsEdit->setPlaceholderText(tr("One argument per line"));
    m_envEdit = new QPlainTextEdit(stdioPage);
    m_envEdit->setPlaceholderText(tr("One KEY=VALUE pair per line"));
    stdioLayout->addRow(tr("Command:"), m_commandEdit);
    stdioLayout->addRow(tr("Arguments:"), m_argsEdit);
    stdioLayout->addRow(tr("Environment:"), m_envEdit);
    m_transportPages->addWidget(stdioPage);

    auto *httpPage = new QWidget(m_transportPages);
    auto *httpLayout = new QFormLayout(httpPage);
    m_urlEdit = new QLineEdit(httpPage);
    m_tokenEdit = new QLineEdit(httpPage);
    m_tokenEdit->setPlaceholderText(tr("Bearer token or ${ENV_VAR}"));
    m_headersEdit = new QPlainTextEdit(httpPage);
    m_headersEdit->setPlaceholderText(tr("One Header: Value pair per line"));
    auto *httpStatusRow = new QWidget(httpPage);
    auto *httpStatusLayout = new QHBoxLayout(httpStatusRow);
    httpStatusLayout->setContentsMargins(0, 0, 0, 0);
    m_httpStatusLabel = new QLabel(notTestedText(), httpStatusRow);
    m_httpStatusLabel->setWordWrap(true);
    m_testConnectionButton = new QPushButton(tr("Test connection"), httpStatusRow);
    m_authorizeButton = new QPushButton(tr("Authorize"), httpStatusRow);
    m_testConnectionButton->setEnabled(false);
    m_authorizeButton->setEnabled(false);
    httpStatusLayout->addWidget(m_httpStatusLabel, 1);
    httpStatusLayout->addWidget(m_testConnectionButton, 0);
    httpStatusLayout->addWidget(m_authorizeButton, 0);
    httpLayout->addRow(tr("URL:"), m_urlEdit);
    httpLayout->addRow(tr("Token:"), m_tokenEdit);
    httpLayout->addRow(tr("Headers:"), m_headersEdit);
    httpLayout->addRow(tr("Status:"), httpStatusRow);
    m_transportPages->addWidget(httpPage);

    auto *unknownPage = new QWidget(m_transportPages);
    auto *unknownLayout = new QVBoxLayout(unknownPage);
    m_unknownTransportLabel = new QLabel(unknownPage);
    m_unknownTransportLabel->setWordWrap(true);
    unknownLayout->addWidget(m_unknownTransportLabel);
    unknownLayout->addStretch(1);
    m_transportPages->addWidget(unknownPage);

    editorLayout->addWidget(m_transportPages, 1);

    mainLayout->addLayout(listColumn, 1);
    mainLayout->addWidget(editorGroup, 2);

    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        const QString name = makeUniqueServerName(QStringLiteral("server"));
        m_servers.insert(name, qtmcp::ServerDefinition{});
        repopulateServerList(name);
        emit changed();
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        const QString name = currentServerName();
        if (name.isEmpty() == true)
        {
            return;
        }
        if (((m_testingConnection && m_connectionTestServerName == name) == true))
        {
            cleanupConnectionTest();
        }
        m_servers.remove(name);
        m_httpStatuses.remove(name);
        m_authorizedServers.remove(name);
        persistConnectionStates();
        repopulateServerList();
        emit changed();
    });
    connect(m_serverList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { loadCurrentServer(); });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() { renameCurrentServer(); });
    connect(m_enabledCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->enabled = state == Qt::Checked;
        }
        invalidateCurrentServerStatus();
    });
    connect(m_transportCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        updateTransportPage(text);
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->transport = text.trimmed();
        }
        invalidateCurrentServerStatus();
    });
    connect(m_startupTimeoutSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->startupTimeoutMs = value;
        }
        invalidateCurrentServerStatus();
    });
    connect(m_requestTimeoutSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->requestTimeoutMs = value;
        }
        invalidateCurrentServerStatus();
    });
    connect(m_commandEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->command = text;
        }
        invalidateCurrentServerStatus();
    });
    connect(m_urlEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->url = text;
        }
        invalidateCurrentServerStatus();
    });
    connect(m_argsEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->args = textLines(m_argsEdit->toPlainText());
        }
        invalidateCurrentServerStatus();
    });
    connect(m_envEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->env = parseMapLines(m_envEdit->toPlainText(), QLatin1Char('='));
        }
        invalidateCurrentServerStatus();
    });
    connect(m_headersEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            QMap<QString, QString> headers =
                parseMapLines(m_headersEdit->toPlainText(), QLatin1Char(':'));
            QString token = m_tokenEdit->text().trimmed();
            if (token.isEmpty() == true)
            {
                QString extractedToken = extractBearerToken(&headers);
                if (extractedToken.isEmpty() == false)
                {
                    m_updating = true;
                    m_tokenEdit->setText(extractedToken);
                    m_headersEdit->setPlainText(
                        joinMapLines(headers, QLatin1Char(':'), true).join(QLatin1Char('\n')));
                    m_updating = false;
                    token = extractedToken;
                }
            }
            if (token.isEmpty() == false)
            {
                removeAuthorizationHeaders(&headers);
                headers.insert(QStringLiteral("Authorization"),
                               QStringLiteral("Bearer %1").arg(token));
            }
            server->headers = headers;
        }
        invalidateCurrentServerStatus();
    });
    connect(m_tokenEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_updating == true)
        {
            return;
        }
        if (auto *server = currentServer(); ((server != nullptr) == true))
        {
            server->headers =
                normalizedHeadersWithToken(m_headersEdit->toPlainText(), m_tokenEdit->text());
        }
        invalidateCurrentServerStatus();
    });
    connect(m_testConnectionButton, &QPushButton::clicked, this,
            [this]() { testCurrentServerConnection(); });
    connect(m_authorizeButton, &QPushButton::clicked, this,
            [this]() { authorizeCurrentServer(); });

    setEditorsEnabled(false);
    m_connectionTestTimer = new QTimer(this);
    m_connectionTestTimer->setSingleShot(true);
    connect(m_connectionTestTimer, &QTimer::timeout, this, [this]() {
        if (((!m_testingConnection || m_connectionTestServerName.isEmpty()) == true))
        {
            return;
        }
        finishConnectionTest(m_connectionTestServerName, HttpStatusKind::Error,
                             tr("Error: connection test timed out"));
    });
}

void McpServersWidget::setServers(const qtmcp::ServerDefinitions &servers)
{
    cleanupConnectionTest();
    m_servers = servers;
    m_httpStatuses.clear();
    m_authorizedServers.clear();
    loadPersistedConnectionStates();
    repopulateServerList();
}

qtmcp::ServerDefinitions McpServersWidget::servers() const
{
    return m_servers;
}

QString McpServersWidget::currentServerName() const
{
    const auto *item = m_serverList->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

qtmcp::ServerDefinition *McpServersWidget::currentServer()
{
    const QString name = currentServerName();
    auto it = m_servers.find(name);
    return it == m_servers.end() ? nullptr : &it.value();
}

const qtmcp::ServerDefinition *McpServersWidget::currentServer() const
{
    const QString name = currentServerName();
    auto it = m_servers.constFind(name);
    return it == m_servers.cend() ? nullptr : &it.value();
}

void McpServersWidget::repopulateServerList(const QString &selectedServer)
{
    const QString targetSelection =
        selectedServer.isEmpty() ? currentServerName() : selectedServer;

    m_updating = true;
    m_serverList->clear();
    for (auto it = m_servers.cbegin(); ((it != m_servers.cend()) == true); ++it)
    {
        auto *item = new QListWidgetItem(it.key(), m_serverList);
        item->setData(Qt::UserRole, it.key());
        m_serverList->addItem(item);
        updateServerListItem(it.key());
    }

    QListWidgetItem *selectedItem = nullptr;
    for (int i = 0; ((i < m_serverList->count()) == true); ++i)
    {
        auto *item = m_serverList->item(i);
        if (((item->data(Qt::UserRole).toString() == targetSelection) == true))
        {
            selectedItem = item;
            break;
        }
    }
    if (((selectedItem == nullptr && m_serverList->count() > 0) == true))
    {
        selectedItem = m_serverList->item(0);
    }

    m_serverList->setCurrentItem(selectedItem);
    m_updating = false;
    loadCurrentServer();
}

void McpServersWidget::loadCurrentServer()
{
    const qtmcp::ServerDefinition *server = currentServer();
    if (((server == nullptr) == true))
    {
        clearEditors();
        setEditorsEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    setEditorsEnabled(true);
    m_removeButton->setEnabled(true);
    m_updating = true;
    m_nameEdit->setText(currentServerName());
    m_enabledCheck->setChecked(server->enabled);
    if (m_transportCombo->findText(server->transport) < 0)
    {
        m_transportCombo->addItem(server->transport);
    }
    m_transportCombo->setCurrentText(server->transport);
    m_startupTimeoutSpin->setValue(server->startupTimeoutMs);
    m_requestTimeoutSpin->setValue(server->requestTimeoutMs);
    m_commandEdit->setText(server->command);
    m_argsEdit->setPlainText(server->args.join(QLatin1Char('\n')));
    m_envEdit->setPlainText(
        joinMapLines(server->env, QLatin1Char('='), false).join(QLatin1Char('\n')));
    m_urlEdit->setText(server->url);
    QMap<QString, QString> visibleHeaders = server->headers;
    const QString token = extractBearerToken(&visibleHeaders);
    m_tokenEdit->setText(token);
    m_headersEdit->setPlainText(
        joinMapLines(visibleHeaders, QLatin1Char(':'), true).join(QLatin1Char('\n')));
    updateTransportPage(server->transport);
    m_updating = false;
    updateCurrentHttpStatusUi();
}

void McpServersWidget::clearEditors()
{
    m_updating = true;
    m_nameEdit->clear();
    m_enabledCheck->setChecked(false);
    m_transportCombo->setCurrentText(QStringLiteral("stdio"));
    m_startupTimeoutSpin->setValue(10000);
    m_requestTimeoutSpin->setValue(30000);
    m_commandEdit->clear();
    m_argsEdit->clear();
    m_envEdit->clear();
    m_urlEdit->clear();
    m_tokenEdit->clear();
    m_headersEdit->clear();
    updateTransportPage(QStringLiteral("stdio"));
    m_updating = false;
    updateCurrentHttpStatusUi();
}

void McpServersWidget::setEditorsEnabled(bool enabled)
{
    m_nameEdit->setEnabled(enabled);
    m_enabledCheck->setEnabled(enabled);
    m_transportCombo->setEnabled(enabled);
    m_startupTimeoutSpin->setEnabled(enabled);
    m_requestTimeoutSpin->setEnabled(enabled);
    m_commandEdit->setEnabled(enabled);
    m_argsEdit->setEnabled(enabled);
    m_envEdit->setEnabled(enabled);
    m_urlEdit->setEnabled(enabled);
    m_tokenEdit->setEnabled(enabled);
    m_headersEdit->setEnabled(enabled);
    m_transportPages->setEnabled(enabled);
    if (enabled == false)
    {
        m_httpStatusLabel->setText(notTestedText());
        m_httpStatusLabel->setStyleSheet(QString());
        m_testConnectionButton->setEnabled(false);
        m_authorizeButton->setEnabled(false);
    }
}

void McpServersWidget::updateTransportPage(const QString &transport)
{
    const QString normalized = transport.trimmed();
    if (((normalized == QStringLiteral("stdio")) == true))
    {
        m_transportPages->setCurrentIndex(0);
    }
    else if (normalized == QStringLiteral("http") || normalized == QStringLiteral("sse"))
    {
        m_transportPages->setCurrentIndex(1);
    }
    else
    {
        m_unknownTransportLabel->setText(
            tr("Transport '%1' is preserved from JSON, but this UI does not yet provide a "
               "dedicated editor for it.")
                .arg(normalized.isEmpty() ? tr("(empty)") : normalized));
        m_transportPages->setCurrentIndex(2);
    }

    updateCurrentHttpStatusUi();
}

void McpServersWidget::updateCurrentHttpStatusUi()
{
    const qtmcp::ServerDefinition *server = currentServer();
    const bool isHttpServer =
        server != nullptr && server->transport.trimmed() == QStringLiteral("http");

    if (isHttpServer == false)
    {
        m_httpStatusLabel->setText(notTestedText());
        m_httpStatusLabel->setStyleSheet(QString());
        m_testConnectionButton->setEnabled(false);
        m_authorizeButton->setEnabled(false);
        return;
    }

    const HttpStatus status = m_httpStatuses.value(currentServerName());
    QString text = status.message;
    if (text.isEmpty() == true)
    {
        text = notTestedText();
    }

    m_httpStatusLabel->setText(text);
    const QColor color = statusColor(status.kind, this);
    m_httpStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color.name(QColor::HexRgb)));

    const bool canTest = !m_authorizing && !m_testingConnection && server->enabled;
    const bool canAuthorize = status.kind == HttpStatusKind::AuthorizationRequired &&
                              !m_authorizing && !m_testingConnection;
    m_testConnectionButton->setEnabled(canTest);
    m_authorizeButton->setEnabled(canAuthorize);
}

void McpServersWidget::updateServerListItem(const QString &serverName)
{
    for (int i = 0; ((i < m_serverList->count()) == true); ++i)
    {
        QListWidgetItem *item = m_serverList->item(i);
        if (((item == nullptr || item->data(Qt::UserRole).toString() != serverName) == true))
        {
            continue;
        }

        const HttpStatus status = m_httpStatuses.value(serverName);
        QString text = serverName;
        if (((!status.message.isEmpty()) == true))
        {
            text += QStringLiteral(" - %1").arg(status.message);
        }
        item->setText(text);
        item->setToolTip(status.message);
        item->setForeground(QBrush(statusColor(status.kind, this)));
        return;
    }
}

void McpServersWidget::setHttpStatus(const QString &serverName, HttpStatusKind kind,
                                     const QString &message)
{
    HttpStatus status;
    status.kind = kind;
    status.message = message;
    m_httpStatuses.insert(serverName, status);
    persistConnectionStates();
    updateServerListItem(serverName);
    if (((serverName == currentServerName()) == true))
    {
        updateCurrentHttpStatusUi();
    }
}

void McpServersWidget::clearHttpStatus(const QString &serverName)
{
    if (serverName.isEmpty() == true)
    {
        return;
    }

    m_httpStatuses.remove(serverName);
    m_authorizedServers.remove(serverName);
    persistConnectionStates();
    updateServerListItem(serverName);
    if (((serverName == currentServerName()) == true))
    {
        updateCurrentHttpStatusUi();
    }
}

void McpServersWidget::loadPersistedConnectionStates()
{
    QString error;
    const McpServerConnectionStates storedStates = loadMcpServerConnectionStates(&error);
    if (error.isEmpty() == false)
    {
        QCAI_WARN("Settings", error);
    }

    for (auto it = storedStates.cbegin(); it != storedStates.cend(); ++it)
    {
        if (((!m_servers.contains(it.key())) == true))
        {
            continue;
        }

        HttpStatus status;
        const QString state = it.value().state.trimmed();
        if (((state == QStringLiteral("authorized")) == true))
        {
            status.kind = HttpStatusKind::Reachable;
            m_authorizedServers.insert(it.key());
            if (it.value().message.trimmed().isEmpty())
            {
                status.message = authorizedText();
            }
        }
        else if (((state == QStringLiteral("reachable")) == true))
        {
            status.kind = HttpStatusKind::Reachable;
        }
        else if (((state == QStringLiteral("authorization_required")) == true))
        {
            status.kind = HttpStatusKind::AuthorizationRequired;
        }
        else if (((state == QStringLiteral("error")) == true))
        {
            status.kind = HttpStatusKind::Error;
        }
        else
        {
            continue;
        }
        if (status.message.isEmpty())
        {
            status.message = it.value().message.trimmed();
        }
        m_httpStatuses.insert(it.key(), status);
        if (status.kind == HttpStatusKind::Reachable &&
            (status.message == authorizedText() ||
             status.message == QStringLiteral("Авторизовано")))
        {
            m_authorizedServers.insert(it.key());
        }
    }

    persistConnectionStates();
}

void McpServersWidget::persistConnectionStates() const
{
    McpServerConnectionStates states;
    for (auto it = m_httpStatuses.cbegin(); it != m_httpStatuses.cend(); ++it)
    {
        if (!m_servers.contains(it.key()))
        {
            continue;
        }

        McpServerConnectionState state;
        switch (it.value().kind)
        {
            case HttpStatusKind::Checking:
                continue;
            case HttpStatusKind::Reachable:
                state.state = m_authorizedServers.contains(it.key()) ? QStringLiteral("authorized")
                                                                     : QStringLiteral("reachable");
                break;
            case HttpStatusKind::AuthorizationRequired:
                state.state = QStringLiteral("authorization_required");
                break;
            case HttpStatusKind::Error:
                state.state = QStringLiteral("error");
                break;
            case HttpStatusKind::Unknown:
                continue;
        }
        state.message = it.value().message;
        states.insert(it.key(), state);
    }

    QString error;
    if (!saveMcpServerConnectionStates(states, &error) && !error.isEmpty())
    {
        QCAI_WARN("Settings", error);
    }
}

void McpServersWidget::finishConnectionTest(const QString &serverName, HttpStatusKind kind,
                                            const QString &message, bool authorized)
{
    if (authorized)
    {
        m_authorizedServers.insert(serverName);
    }
    else if (kind != HttpStatusKind::Reachable || message != authorizedText())
    {
        m_authorizedServers.remove(serverName);
    }

    setHttpStatus(serverName, kind, message);
    cleanupConnectionTest();
}

void McpServersWidget::cleanupConnectionTest()
{
    if (m_connectionTestTimer != nullptr)
    {
        m_connectionTestTimer->stop();
    }

    qtmcp::Client *client = m_connectionTestClient;
    m_connectionTestClient = nullptr;
    m_connectionTestServerName.clear();
    m_connectionTestRequestId = 0;
    m_testingConnection = false;

    if (client != nullptr)
    {
        client->disconnect(this);
        client->stop();
        client->deleteLater();
    }

    updateCurrentHttpStatusUi();
}

void McpServersWidget::invalidateCurrentServerStatus()
{
    const QString serverName = currentServerName();
    if (serverName.isEmpty())
    {
        return;
    }

    if (m_testingConnection && m_connectionTestServerName == serverName)
    {
        cleanupConnectionTest();
    }
    clearHttpStatus(serverName);
    emit changed();
}

void McpServersWidget::testCurrentServerConnection()
{
    if (m_testingConnection || m_authorizing)
    {
        return;
    }

    const QString serverName = currentServerName();
    const qtmcp::ServerDefinition *definition = currentServer();
    if (serverName.isEmpty() || definition == nullptr ||
        definition->transport.trimmed() != QStringLiteral("http"))
    {
        return;
    }

    if (!definition->enabled)
    {
        setHttpStatus(serverName, HttpStatusKind::Error, tr("Error: server is disabled"));
        return;
    }

    if (definition->url.trimmed().isEmpty())
    {
        setHttpStatus(serverName, HttpStatusKind::Error, tr("Error: URL is not set"));
        return;
    }

    m_testingConnection = true;
    m_connectionTestServerName = serverName;
    m_connectionTestRequestId = 0;
    setHttpStatus(serverName, HttpStatusKind::Checking, checkingText());
    const int startupTimeoutMs = probeTimeoutMs(definition->startupTimeoutMs);
    const int requestTimeoutMs = probeTimeoutMs(definition->requestTimeoutMs);

    qtmcp::HttpTransportConfig transportConfig = buildHttpTransportConfig(serverName, *definition);
    transportConfig.interactiveOAuthEnabled = false;

    auto *client = new qtmcp::Client(this);
    client->setTransport(std::make_unique<qtmcp::HttpTransport>(transportConfig));
    m_connectionTestClient = client;

    auto *transport = qobject_cast<qtmcp::HttpTransport *>(client->transport());
    Q_ASSERT(transport != nullptr);

    connect(client, &qtmcp::Client::transportLogMessage, this,
            [serverName](const QString &message) { logMcpSettingsDebug(serverName, message); });
    connect(client, &qtmcp::Client::transportErrorOccurred, this,
            [this, client, transport, serverName](const QString &message) {
                if (client != m_connectionTestClient)
                {
                    return;
                }
                QCAI_ERROR("MCP",
                           QStringLiteral("[settings:%1] %2").arg(serverName, message.trimmed()));

                const QString transportOAuthError = transport->lastOAuthError().trimmed();
                if (transport->lastAuthorizationRequired() ||
                    transportOAuthError == QStringLiteral("OAuth authorization required.") ||
                    message.trimmed() == QStringLiteral("OAuth authorization required."))
                {
                    finishConnectionTest(serverName, HttpStatusKind::AuthorizationRequired,
                                         authorizationRequiredText());
                    return;
                }

                const QString details =
                    !transportOAuthError.isEmpty() ? transportOAuthError : message.trimmed();
                finishConnectionTest(serverName, HttpStatusKind::Error,
                                     details.isEmpty() ? errorPrefixText()
                                                       : tr("Error: %1").arg(details));
            });

    connect(
        client, &qtmcp::Client::connected, this,
        [this, client, transport, serverName, requestTimeoutMs]() {
            if (client != m_connectionTestClient)
            {
                return;
            }

            if (m_connectionTestTimer != nullptr)
            {
                m_connectionTestTimer->start(requestTimeoutMs);
            }

            if (transport->config().oauthEnabled && m_authorizedServers.contains(serverName) &&
                transport->hasCachedOAuthCredentials())
            {
                logMcpSettingsDebug(
                    serverName,
                    QStringLiteral(
                        "Skipping redundant HTTP initialize probe because this server is already "
                        "authorized and cached OAuth credentials are available."));
                finishConnectionTest(serverName, HttpStatusKind::Reachable, authorizedText(),
                                     true);
                return;
            }

            if (transport->config().oauthEnabled && !transport->hasCachedOAuthCredentials())
            {
                logMcpSettingsDebug(
                    serverName,
                    QStringLiteral("Skipping HTTP initialize probe because no cached OAuth "
                                   "credentials are available; authorization is required first."));
                finishConnectionTest(serverName, HttpStatusKind::AuthorizationRequired,
                                     authorizationRequiredText());
                return;
            }

            const qint64 requestId =
                client->sendRequest(QStringLiteral("initialize"), initializeParamsObject());
            if (requestId <= 0)
            {
                finishConnectionTest(serverName, HttpStatusKind::Error,
                                     tr("Error: failed to send initialize request"));
                return;
            }
            m_connectionTestRequestId = requestId;
        });

    connect(client, &qtmcp::Client::responseReceived, this,
            [this, client, transport, serverName](const QJsonValue &id, const QJsonValue &) {
                if (client != m_connectionTestClient ||
                    id.toInteger() != m_connectionTestRequestId)
                {
                    return;
                }
                finishConnectionTest(serverName, HttpStatusKind::Reachable,
                                     transport->config().oauthEnabled ? authorizedText()
                                                                      : connectedText(),
                                     transport->config().oauthEnabled);
            });

    connect(client, &qtmcp::Client::errorResponseReceived, this,
            [this, client, transport, serverName](const QJsonValue &id, int,
                                                  const QString &message, const QJsonValue &) {
                if (client != m_connectionTestClient ||
                    id.toInteger() != m_connectionTestRequestId)
                {
                    return;
                }

                const QString transportOAuthError = transport->lastOAuthError().trimmed();
                if (transport->lastAuthorizationRequired() ||
                    transportOAuthError == QStringLiteral("OAuth authorization required."))
                {
                    finishConnectionTest(serverName, HttpStatusKind::AuthorizationRequired,
                                         authorizationRequiredText());
                    return;
                }

                const QString details =
                    !transportOAuthError.isEmpty() ? transportOAuthError : message.trimmed();
                finishConnectionTest(serverName, HttpStatusKind::Error,
                                     details.isEmpty() ? errorPrefixText()
                                                       : tr("Error: %1").arg(details));
            });

    connect(client, &qtmcp::Client::disconnected, this, [this, client, serverName]() {
        if (client != m_connectionTestClient)
        {
            return;
        }
        finishConnectionTest(serverName, HttpStatusKind::Error,
                             tr("Error: MCP server disconnected"));
    });

    if (m_connectionTestTimer != nullptr)
    {
        m_connectionTestTimer->start(startupTimeoutMs);
    }
    client->start();
}

void McpServersWidget::authorizeCurrentServer()
{
    if (m_authorizing || m_testingConnection)
    {
        return;
    }

    const QString serverName = currentServerName();
    const qtmcp::ServerDefinition *definition = currentServer();
    if (serverName.isEmpty() || definition == nullptr ||
        definition->transport.trimmed() != QStringLiteral("http"))
    {
        return;
    }

    m_authorizing = true;
    setHttpStatus(serverName, HttpStatusKind::Checking, authorizingText());
    updateCurrentHttpStatusUi();

    qtmcp::HttpTransportConfig transportConfig = buildHttpTransportConfig(serverName, *definition);
    transportConfig.oauthEnabled = true;

    qtmcp::HttpTransport transport(std::move(transportConfig));
    connect(&transport, &qtmcp::Transport::logMessage, this,
            [serverName](const QString &message) { logMcpSettingsDebug(serverName, message); });
    connect(&transport, &qtmcp::Transport::errorOccurred, this,
            [serverName](const QString &message) {
                QCAI_ERROR("MCP",
                           QStringLiteral("[settings:%1] %2").arg(serverName, message.trimmed()));
            });
    QString errorMessage;
    logMcpSettingsDebug(serverName,
                        QStringLiteral("Starting explicit OAuth authorization from settings UI."));
    const bool authorized = transport.authorize(&errorMessage);
    transport.stop();
    logMcpSettingsDebug(
        serverName, QStringLiteral("Explicit OAuth authorization finished: success=%1 message=%2")
                        .arg(authorized ? QStringLiteral("yes") : QStringLiteral("no"),
                             errorMessage.isEmpty() ? QStringLiteral("(none)") : errorMessage));

    m_authorizing = false;
    if (!authorized)
    {
        m_authorizedServers.remove(serverName);
        setHttpStatus(serverName, HttpStatusKind::AuthorizationRequired,
                      authorizationRequiredText());
        if (!errorMessage.trimmed().isEmpty())
        {
            QMessageBox::warning(this, tr("OAuth Authorization Failed"), errorMessage.trimmed());
        }
        updateCurrentHttpStatusUi();
        return;
    }

    m_authorizedServers.insert(serverName);
    setHttpStatus(serverName, HttpStatusKind::Reachable, authorizedText());
    updateCurrentHttpStatusUi();
}

QString McpServersWidget::makeUniqueServerName(const QString &baseName) const
{
    QString candidate = baseName;
    int suffix = 2;
    while (m_servers.contains(candidate))
    {
        candidate = QStringLiteral("%1-%2").arg(baseName).arg(suffix++);
    }
    return candidate;
}

void McpServersWidget::renameCurrentServer()
{
    if (m_updating)
    {
        return;
    }

    const QString oldName = currentServerName();
    if (oldName.isEmpty())
    {
        return;
    }

    if (m_testingConnection && m_connectionTestServerName == oldName)
    {
        cleanupConnectionTest();
    }

    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty())
    {
        QMessageBox::warning(this, tr("Invalid MCP Server Name"),
                             tr("The MCP server name must not be empty."));
        m_updating = true;
        m_nameEdit->setText(oldName);
        m_updating = false;
        return;
    }

    if (newName != oldName && m_servers.contains(newName))
    {
        QMessageBox::warning(this, tr("Duplicate MCP Server Name"),
                             tr("An MCP server named '%1' already exists.").arg(newName));
        m_updating = true;
        m_nameEdit->setText(oldName);
        m_updating = false;
        return;
    }

    if (newName == oldName)
    {
        return;
    }

    const HttpStatus preservedStatus = m_httpStatuses.take(oldName);
    const bool wasAuthorized = m_authorizedServers.remove(oldName);
    const qtmcp::ServerDefinition definition = m_servers.take(oldName);
    m_servers.insert(newName, definition);
    if (preservedStatus.kind != HttpStatusKind::Unknown || !preservedStatus.message.isEmpty())
    {
        m_httpStatuses.insert(newName, preservedStatus);
    }
    if (wasAuthorized)
    {
        m_authorizedServers.insert(newName);
    }
    persistConnectionStates();
    repopulateServerList(newName);
    emit changed();
}

QStringList McpServersWidget::textLines(const QString &text)
{
    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n')))
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            lines.append(trimmed);
        }
    }
    return lines;
}

QStringList McpServersWidget::joinMapLines(const QMap<QString, QString> &values, QChar separator,
                                           bool insertSpaceAfterSeparator)
{
    QStringList lines;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        const QString glue = insertSpaceAfterSeparator ? QString(separator) + QStringLiteral(" ")
                                                       : QString(separator);
        lines.append(it.key() + glue + it.value());
    }
    return lines;
}

QMap<QString, QString> McpServersWidget::parseMapLines(const QString &text, QChar separator)
{
    QMap<QString, QString> values;
    for (const QString &line : textLines(text))
    {
        const qsizetype separatorIndex = line.indexOf(separator);
        if (separatorIndex < 0)
        {
            values.insert(line.trimmed(), QString());
            continue;
        }

        const QString key = line.left(separatorIndex).trimmed();
        const QString value = line.mid(separatorIndex + 1).trimmed();
        if (!key.isEmpty())
        {
            values.insert(key, value);
        }
    }
    return values;
}

}  // namespace qcai2
