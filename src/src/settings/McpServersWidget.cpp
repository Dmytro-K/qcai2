/*! Implements the settings widget used to edit global MCP server definitions. */

#include "McpServersWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace qcai2
{

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
    m_transportCombo->addItems({QStringLiteral("stdio"), QStringLiteral("http")});
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
    m_headersEdit = new QPlainTextEdit(httpPage);
    m_headersEdit->setPlaceholderText(tr("One Header: Value pair per line"));
    httpLayout->addRow(tr("URL:"), m_urlEdit);
    httpLayout->addRow(tr("Headers:"), m_headersEdit);
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
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        const QString name = currentServerName();
        if (name.isEmpty())
            return;
        m_servers.remove(name);
        repopulateServerList();
    });
    connect(m_serverList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { loadCurrentServer(); });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() { renameCurrentServer(); });
    connect(m_enabledCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->enabled = state == Qt::Checked;
    });
    connect(m_transportCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        updateTransportPage(text);
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->transport = text.trimmed();
    });
    connect(m_startupTimeoutSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->startupTimeoutMs = value;
    });
    connect(m_requestTimeoutSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->requestTimeoutMs = value;
    });
    connect(m_commandEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->command = text;
    });
    connect(m_urlEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->url = text;
    });
    connect(m_argsEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->args = textLines(m_argsEdit->toPlainText());
    });
    connect(m_envEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->env = parseMapLines(m_envEdit->toPlainText(), QLatin1Char('='));
    });
    connect(m_headersEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_updating)
            return;
        if (auto *server = currentServer())
            server->headers = parseMapLines(m_headersEdit->toPlainText(), QLatin1Char(':'));
    });

    setEditorsEnabled(false);
}

void McpServersWidget::setServers(const qtmcp::ServerDefinitions &servers)
{
    m_servers = servers;
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
    const QString targetSelection = selectedServer.isEmpty() ? currentServerName() : selectedServer;

    m_updating = true;
    m_serverList->clear();
    for (auto it = m_servers.cbegin(); it != m_servers.cend(); ++it)
    {
        auto *item = new QListWidgetItem(it.key(), m_serverList);
        item->setData(Qt::UserRole, it.key());
        m_serverList->addItem(item);
    }

    QListWidgetItem *selectedItem = nullptr;
    for (int i = 0; i < m_serverList->count(); ++i)
    {
        auto *item = m_serverList->item(i);
        if (item->data(Qt::UserRole).toString() == targetSelection)
        {
            selectedItem = item;
            break;
        }
    }
    if (selectedItem == nullptr && m_serverList->count() > 0)
        selectedItem = m_serverList->item(0);

    m_serverList->setCurrentItem(selectedItem);
    m_updating = false;
    loadCurrentServer();
}

void McpServersWidget::loadCurrentServer()
{
    const qtmcp::ServerDefinition *server = currentServer();
    if (server == nullptr)
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
        m_transportCombo->addItem(server->transport);
    m_transportCombo->setCurrentText(server->transport);
    m_startupTimeoutSpin->setValue(server->startupTimeoutMs);
    m_requestTimeoutSpin->setValue(server->requestTimeoutMs);
    m_commandEdit->setText(server->command);
    m_argsEdit->setPlainText(server->args.join(QLatin1Char('\n')));
    m_envEdit->setPlainText(
        joinMapLines(server->env, QLatin1Char('='), false).join(QLatin1Char('\n')));
    m_urlEdit->setText(server->url);
    m_headersEdit->setPlainText(
        joinMapLines(server->headers, QLatin1Char(':'), true).join(QLatin1Char('\n')));
    updateTransportPage(server->transport);
    m_updating = false;
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
    m_headersEdit->clear();
    updateTransportPage(QStringLiteral("stdio"));
    m_updating = false;
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
    m_headersEdit->setEnabled(enabled);
    m_transportPages->setEnabled(enabled);
}

void McpServersWidget::updateTransportPage(const QString &transport)
{
    const QString normalized = transport.trimmed();
    if (normalized == QStringLiteral("stdio"))
    {
        m_transportPages->setCurrentIndex(0);
    }
    else if (normalized == QStringLiteral("http"))
    {
        m_transportPages->setCurrentIndex(1);
    }
    else
    {
        m_unknownTransportLabel->setText(
            tr("Transport '%1' is preserved from JSON, but this UI does not yet provide a dedicated editor for it.")
                .arg(normalized.isEmpty() ? tr("(empty)") : normalized));
        m_transportPages->setCurrentIndex(2);
    }
}

QString McpServersWidget::makeUniqueServerName(const QString &baseName) const
{
    QString candidate = baseName;
    int suffix = 2;
    while (m_servers.contains(candidate))
        candidate = QStringLiteral("%1-%2").arg(baseName).arg(suffix++);
    return candidate;
}

void McpServersWidget::renameCurrentServer()
{
    if (m_updating)
        return;

    const QString oldName = currentServerName();
    if (oldName.isEmpty())
        return;

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
        return;

    const qtmcp::ServerDefinition definition = m_servers.take(oldName);
    m_servers.insert(newName, definition);
    repopulateServerList(newName);
}

QStringList McpServersWidget::textLines(const QString &text)
{
    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n')))
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            lines.append(trimmed);
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
            values.insert(key, value);
    }
    return values;
}

}  // namespace qcai2
