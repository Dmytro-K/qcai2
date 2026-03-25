/*! Implements the settings widget used to edit global MCP server definitions. */

#include "mcp_servers_widget.h"

#include <qtmcp/client.h>
#include <qtmcp/http_transport.h>

#include "../util/logger.h"
#include "../util/migration.h"
#include "settings.h"
#include "settings_spin_box.h"

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

QString extra_field_string(const QJsonObject &extra_fields, const QString &name)
{
    return extra_fields.value(name).toString().trimmed();
}

bool extra_field_bool(const QJsonObject &extra_fields, const QString &name, bool fallback)
{
    const QJsonValue value = extra_fields.value(name);
    return value.isBool() ? value.toBool() : fallback;
}

QStringList extra_field_string_list(const QJsonObject &extra_fields, const QString &name)
{
    QStringList values;
    const QJsonValue value = extra_fields.value(name);
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
                     {QStringLiteral("version"), Migration::current_revision_string()}}}};
}

int probe_timeout_ms(int configuredTimeoutMs)
{
    if (((configuredTimeoutMs <= 0) == true))
    {
        return kStatusProbeTimeoutMs;
    }
    return std::min(configuredTimeoutMs, kStatusProbeTimeoutMs);
}

qtmcp::http_transport_config_t
buildHttpTransportConfig(const QString &serverName, const qtmcp::server_definition_t &definition)
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    qtmcp::http_transport_config_t config;
    config.endpoint = QUrl(expandEnvironmentPlaceholders(definition.url, environment));
    config.protocol_version = QString::fromLatin1(kMcpProtocolVersionHttp);
    config.request_timeout_ms = definition.request_timeout_ms;
    config.oauth_enabled = extra_field_bool(
        definition.extra_fields, QStringLiteral("oauthEnabled"), definition.headers.isEmpty());
    config.oauth_client_id =
        extra_field_string(definition.extra_fields, QStringLiteral("oauthClientId"));
    config.oauth_client_secret = expandEnvironmentPlaceholders(
        extra_field_string(definition.extra_fields, QStringLiteral("oauthClientSecret")),
        environment);
    config.oauth_client_name =
        extra_field_string(definition.extra_fields, QStringLiteral("oauthClientName"));
    if (config.oauth_client_name.isEmpty())
    {
        config.oauth_client_name = QStringLiteral("qcai2");
    }
    config.oauth_scopes =
        extra_field_string_list(definition.extra_fields, QStringLiteral("oauthScopes"));
    for (auto it = definition.headers.begin(); it != definition.headers.end(); ++it)
    {
        config.headers.insert(it.key(), expandEnvironmentPlaceholders(it.value(), environment));
    }

    Q_UNUSED(serverName);
    return config;
}

QColor statusColor(mcp_servers_widget_t::http_status_kind_t kind, const QWidget *widget)
{
    switch (kind)
    {
        case mcp_servers_widget_t::http_status_kind_t::REACHABLE:
            return QColor(0, 128, 0);
        case mcp_servers_widget_t::http_status_kind_t::AUTHORIZATION_REQUIRED:
        case mcp_servers_widget_t::http_status_kind_t::ERROR:
            return QColor(180, 0, 0);
        default:
            return widget->palette().color(QPalette::Text);
    }
}

QString notTestedText()
{
    return mcp_servers_widget_t::tr("Not tested");
}

QString checkingText()
{
    return mcp_servers_widget_t::tr("Checking...");
}

QString authorizedText()
{
    return mcp_servers_widget_t::tr("Authorized");
}

QString connectedText()
{
    return mcp_servers_widget_t::tr("Connected");
}

QString authorizationRequiredText()
{
    return mcp_servers_widget_t::tr("Authorization required");
}

QString authorizingText()
{
    return mcp_servers_widget_t::tr("Authorizing...");
}

QString errorPrefixText()
{
    return mcp_servers_widget_t::tr("Error");
}

}  // namespace

mcp_servers_widget_t::mcp_servers_widget_t(QWidget *parent) : QWidget(parent)
{
    const qtmcp::server_definition_t default_server_definition;

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *listColumn = new QVBoxLayout;
    this->server_list = new QListWidget(this);
    listColumn->addWidget(this->server_list, 1);

    auto *buttonRow = new QHBoxLayout;
    this->add_button = new QPushButton(tr("Add"), this);
    this->remove_button = new QPushButton(tr("Remove"), this);
    buttonRow->addWidget(this->add_button);
    buttonRow->addWidget(this->remove_button);
    buttonRow->addStretch(1);
    listColumn->addLayout(buttonRow);

    auto *editorGroup = new QGroupBox(tr("Server"), this);
    auto *editorLayout = new QVBoxLayout(editorGroup);
    auto *formLayout = new QFormLayout;

    this->name_edit = new QLineEdit(editorGroup);
    this->enabled_check = new QCheckBox(tr("Enabled"), editorGroup);
    this->transport_combo = new QComboBox(editorGroup);
    this->transport_combo->setEditable(true);
    this->transport_combo->addItems(
        {QStringLiteral("stdio"), QStringLiteral("http"), QStringLiteral("sse")});
    this->startup_timeout_spin = new settings_spin_box_t(editorGroup);
    this->startup_timeout_spin->setMinimum(0);
    this->startup_timeout_spin->setMaximum(600000);
    this->startup_timeout_spin->setSuffix(tr(" ms"));
    this->startup_timeout_spin->set_default_value(default_server_definition.startup_timeout_ms);
    this->request_timeout_spin = new settings_spin_box_t(editorGroup);
    this->request_timeout_spin->setMinimum(0);
    this->request_timeout_spin->setMaximum(600000);
    this->request_timeout_spin->setSuffix(tr(" ms"));
    this->request_timeout_spin->set_default_value(default_server_definition.request_timeout_ms);

    formLayout->addRow(tr("Name:"), this->name_edit);
    formLayout->addRow(QString(), this->enabled_check);
    formLayout->addRow(tr("transport_t:"), this->transport_combo);
    formLayout->addRow(tr("Startup timeout:"), this->startup_timeout_spin);
    formLayout->addRow(tr("Request timeout:"), this->request_timeout_spin);
    editorLayout->addLayout(formLayout);

    this->transport_pages = new QStackedWidget(editorGroup);

    auto *stdioPage = new QWidget(this->transport_pages);
    auto *stdioLayout = new QFormLayout(stdioPage);
    this->command_edit = new QLineEdit(stdioPage);
    this->args_edit = new QPlainTextEdit(stdioPage);
    this->args_edit->setPlaceholderText(tr("One argument per line"));
    this->env_edit = new QPlainTextEdit(stdioPage);
    this->env_edit->setPlaceholderText(tr("One KEY=VALUE pair per line"));
    stdioLayout->addRow(tr("Command:"), this->command_edit);
    stdioLayout->addRow(tr("Arguments:"), this->args_edit);
    stdioLayout->addRow(tr("Environment:"), this->env_edit);
    this->transport_pages->addWidget(stdioPage);

    auto *httpPage = new QWidget(this->transport_pages);
    auto *httpLayout = new QFormLayout(httpPage);
    this->url_edit = new QLineEdit(httpPage);
    this->token_edit = new QLineEdit(httpPage);
    this->token_edit->setPlaceholderText(tr("Bearer token or ${ENV_VAR}"));
    this->headers_edit = new QPlainTextEdit(httpPage);
    this->headers_edit->setPlaceholderText(tr("One Header: Value pair per line"));
    auto *httpStatusRow = new QWidget(httpPage);
    auto *httpStatusLayout = new QHBoxLayout(httpStatusRow);
    httpStatusLayout->setContentsMargins(0, 0, 0, 0);
    this->http_status_label = new QLabel(notTestedText(), httpStatusRow);
    this->http_status_label->setWordWrap(true);
    this->test_connection_button = new QPushButton(tr("Test connection"), httpStatusRow);
    this->authorize_button = new QPushButton(tr("Authorize"), httpStatusRow);
    this->test_connection_button->setEnabled(false);
    this->authorize_button->setEnabled(false);
    httpStatusLayout->addWidget(this->http_status_label, 1);
    httpStatusLayout->addWidget(this->test_connection_button, 0);
    httpStatusLayout->addWidget(this->authorize_button, 0);
    httpLayout->addRow(tr("URL:"), this->url_edit);
    httpLayout->addRow(tr("Token:"), this->token_edit);
    httpLayout->addRow(tr("Headers:"), this->headers_edit);
    httpLayout->addRow(tr("Status:"), httpStatusRow);
    this->transport_pages->addWidget(httpPage);

    auto *unknownPage = new QWidget(this->transport_pages);
    auto *unknownLayout = new QVBoxLayout(unknownPage);
    this->unknown_transport_label = new QLabel(unknownPage);
    this->unknown_transport_label->setWordWrap(true);
    unknownLayout->addWidget(this->unknown_transport_label);
    unknownLayout->addStretch(1);
    this->transport_pages->addWidget(unknownPage);

    editorLayout->addWidget(this->transport_pages, 1);

    mainLayout->addLayout(listColumn, 1);
    mainLayout->addWidget(editorGroup, 2);

    connect(this->add_button, &QPushButton::clicked, this, [this]() {
        const QString name = this->make_unique_server_name(QStringLiteral("server"));
        this->server_definitions.insert(name, qtmcp::server_definition_t{});
        this->repopulate_server_list(name);
        emit this->changed();
    });
    connect(this->remove_button, &QPushButton::clicked, this, [this]() {
        const QString name = this->current_server_name();
        if (name.isEmpty() == true)
        {
            return;
        }
        if (((this->testing_connection && this->connection_test_server_name == name) == true))
        {
            this->cleanup_connection_test();
        }
        this->server_definitions.remove(name);
        this->http_statuses.remove(name);
        this->authorized_servers.remove(name);
        this->persist_connection_states();
        this->repopulate_server_list();
        emit this->changed();
    });
    connect(this->server_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { this->load_current_server(); });
    connect(this->name_edit, &QLineEdit::editingFinished, this,
            [this]() { this->rename_current_server(); });
    connect(this->enabled_check, &QCheckBox::checkStateChanged, this,
            [this](Qt::CheckState state) {
                if (this->updating == true)
                {
                    return;
                }
                if (auto *server = this->current_server(); ((server != nullptr) == true))
                {
                    server->enabled = state == Qt::Checked;
                }
                this->invalidate_current_server_status();
            });
    connect(this->transport_combo, &QComboBox::currentTextChanged, this,
            [this](const QString &text) {
                this->update_transport_page(text);
                if (this->updating == true)
                {
                    return;
                }
                if (auto *server = this->current_server(); ((server != nullptr) == true))
                {
                    server->transport = text.trimmed();
                }
                this->invalidate_current_server_status();
            });
    connect(this->startup_timeout_spin, &settings_spin_box_t::value_changed, this,
            [this](int value) {
                if (this->updating == true)
                {
                    return;
                }
                if (auto *server = this->current_server(); ((server != nullptr) == true))
                {
                    server->startup_timeout_ms = value;
                }
                this->invalidate_current_server_status();
            });
    connect(this->request_timeout_spin, &settings_spin_box_t::value_changed, this,
            [this](int value) {
                if (this->updating == true)
                {
                    return;
                }
                if (auto *server = this->current_server(); ((server != nullptr) == true))
                {
                    server->request_timeout_ms = value;
                }
                this->invalidate_current_server_status();
            });
    connect(this->command_edit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (this->updating == true)
        {
            return;
        }
        if (auto *server = this->current_server(); ((server != nullptr) == true))
        {
            server->command = text;
        }
        this->invalidate_current_server_status();
    });
    connect(this->url_edit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (this->updating == true)
        {
            return;
        }
        if (auto *server = this->current_server(); ((server != nullptr) == true))
        {
            server->url = text;
        }
        this->invalidate_current_server_status();
    });
    connect(this->args_edit, &QPlainTextEdit::textChanged, this, [this]() {
        if (this->updating == true)
        {
            return;
        }
        if (auto *server = this->current_server(); ((server != nullptr) == true))
        {
            server->args = this->text_lines(this->args_edit->toPlainText());
        }
        this->invalidate_current_server_status();
    });
    connect(this->env_edit, &QPlainTextEdit::textChanged, this, [this]() {
        if (this->updating == true)
        {
            return;
        }
        if (auto *server = this->current_server(); ((server != nullptr) == true))
        {
            server->env = this->parse_map_lines(this->env_edit->toPlainText(), QLatin1Char('='));
        }
        this->invalidate_current_server_status();
    });
    connect(this->headers_edit, &QPlainTextEdit::textChanged, this, [this]() {
        if (this->updating == true)
        {
            return;
        }
        if (auto *server = this->current_server(); ((server != nullptr) == true))
        {
            QMap<QString, QString> headers =
                this->parse_map_lines(this->headers_edit->toPlainText(), QLatin1Char(':'));
            QString token = this->token_edit->text().trimmed();
            if (token.isEmpty() == true)
            {
                QString extractedToken = extractBearerToken(&headers);
                if (extractedToken.isEmpty() == false)
                {
                    this->updating = true;
                    this->token_edit->setText(extractedToken);
                    this->headers_edit->setPlainText(
                        this->join_map_lines(headers, QLatin1Char(':'), true)
                            .join(QLatin1Char('\n')));
                    this->updating = false;
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
        this->invalidate_current_server_status();
    });
    connect(this->token_edit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (this->updating == true)
        {
            return;
        }
        if (auto *server = this->current_server(); ((server != nullptr) == true))
        {
            server->headers = normalizedHeadersWithToken(this->headers_edit->toPlainText(),
                                                         this->token_edit->text());
        }
        this->invalidate_current_server_status();
    });
    connect(this->test_connection_button, &QPushButton::clicked, this,
            [this]() { this->test_current_server_connection(); });
    connect(this->authorize_button, &QPushButton::clicked, this,
            [this]() { this->authorize_current_server(); });

    this->set_editors_enabled(false);
    this->connection_test_timer = new QTimer(this);
    this->connection_test_timer->setSingleShot(true);
    connect(this->connection_test_timer, &QTimer::timeout, this, [this]() {
        if (((!this->testing_connection || this->connection_test_server_name.isEmpty()) == true))
        {
            return;
        }
        this->finish_connection_test(this->connection_test_server_name, http_status_kind_t::ERROR,
                                     tr("Error: connection test timed out"));
    });
}

void mcp_servers_widget_t::set_servers(const qtmcp::server_definitions_t &servers)
{
    this->cleanup_connection_test();
    this->server_definitions = servers;
    this->http_statuses.clear();
    this->authorized_servers.clear();
    this->load_persisted_connection_states();
    this->repopulate_server_list();
}

qtmcp::server_definitions_t mcp_servers_widget_t::servers() const
{
    return this->server_definitions;
}

QString mcp_servers_widget_t::current_server_name() const
{
    const auto *item = this->server_list->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

qtmcp::server_definition_t *mcp_servers_widget_t::current_server()
{
    const QString name = this->current_server_name();
    auto it = this->server_definitions.find(name);
    return it == this->server_definitions.end() ? nullptr : &it.value();
}

const qtmcp::server_definition_t *mcp_servers_widget_t::current_server() const
{
    const QString name = this->current_server_name();
    auto it = this->server_definitions.constFind(name);
    return it == this->server_definitions.cend() ? nullptr : &it.value();
}

void mcp_servers_widget_t::repopulate_server_list(const QString &selectedServer)
{
    const QString targetSelection =
        selectedServer.isEmpty() ? this->current_server_name() : selectedServer;

    this->updating = true;
    this->server_list->clear();
    for (auto it = this->server_definitions.cbegin();
         ((it != this->server_definitions.cend()) == true); ++it)
    {
        auto *item = new QListWidgetItem(it.key(), this->server_list);
        item->setData(Qt::UserRole, it.key());
        this->server_list->addItem(item);
        this->update_server_list_item(it.key());
    }

    QListWidgetItem *selectedItem = nullptr;
    for (int i = 0; ((i < this->server_list->count()) == true); ++i)
    {
        auto *item = this->server_list->item(i);
        if (((item->data(Qt::UserRole).toString() == targetSelection) == true))
        {
            selectedItem = item;
            break;
        }
    }
    if (((selectedItem == nullptr && this->server_list->count() > 0) == true))
    {
        selectedItem = this->server_list->item(0);
    }

    this->server_list->setCurrentItem(selectedItem);
    this->updating = false;
    this->load_current_server();
}

void mcp_servers_widget_t::load_current_server()
{
    const qtmcp::server_definition_t *server = this->current_server();
    if (((server == nullptr) == true))
    {
        this->clear_editors();
        this->set_editors_enabled(false);
        this->remove_button->setEnabled(false);
        return;
    }

    this->set_editors_enabled(true);
    this->remove_button->setEnabled(true);
    this->updating = true;
    this->name_edit->setText(this->current_server_name());
    this->enabled_check->setChecked(server->enabled);
    if (this->transport_combo->findText(server->transport) < 0)
    {
        this->transport_combo->addItem(server->transport);
    }
    this->transport_combo->setCurrentText(server->transport);
    this->startup_timeout_spin->set_value(server->startup_timeout_ms);
    this->request_timeout_spin->set_value(server->request_timeout_ms);
    this->command_edit->setText(server->command);
    this->args_edit->setPlainText(server->args.join(QLatin1Char('\n')));
    this->env_edit->setPlainText(
        this->join_map_lines(server->env, QLatin1Char('='), false).join(QLatin1Char('\n')));
    this->url_edit->setText(server->url);
    QMap<QString, QString> visibleHeaders = server->headers;
    const QString token = extractBearerToken(&visibleHeaders);
    this->token_edit->setText(token);
    this->headers_edit->setPlainText(
        this->join_map_lines(visibleHeaders, QLatin1Char(':'), true).join(QLatin1Char('\n')));
    this->update_transport_page(server->transport);
    this->updating = false;
    this->update_current_http_status_ui();
}

void mcp_servers_widget_t::clear_editors()
{
    this->updating = true;
    this->name_edit->clear();
    this->enabled_check->setChecked(false);
    this->transport_combo->setCurrentText(QStringLiteral("stdio"));
    this->startup_timeout_spin->set_value(10000);
    this->request_timeout_spin->set_value(30000);
    this->command_edit->clear();
    this->args_edit->clear();
    this->env_edit->clear();
    this->url_edit->clear();
    this->token_edit->clear();
    this->headers_edit->clear();
    this->update_transport_page(QStringLiteral("stdio"));
    this->updating = false;
    this->update_current_http_status_ui();
}

void mcp_servers_widget_t::set_editors_enabled(bool enabled)
{
    this->name_edit->setEnabled(enabled);
    this->enabled_check->setEnabled(enabled);
    this->transport_combo->setEnabled(enabled);
    this->startup_timeout_spin->setEnabled(enabled);
    this->request_timeout_spin->setEnabled(enabled);
    this->command_edit->setEnabled(enabled);
    this->args_edit->setEnabled(enabled);
    this->env_edit->setEnabled(enabled);
    this->url_edit->setEnabled(enabled);
    this->token_edit->setEnabled(enabled);
    this->headers_edit->setEnabled(enabled);
    this->transport_pages->setEnabled(enabled);
    if (enabled == false)
    {
        this->http_status_label->setText(notTestedText());
        this->http_status_label->setStyleSheet(QString());
        this->test_connection_button->setEnabled(false);
        this->authorize_button->setEnabled(false);
    }
}

void mcp_servers_widget_t::update_transport_page(const QString &transport)
{
    const QString normalized = transport.trimmed();
    if (((normalized == QStringLiteral("stdio")) == true))
    {
        this->transport_pages->setCurrentIndex(0);
    }
    else if (normalized == QStringLiteral("http") || normalized == QStringLiteral("sse"))
    {
        this->transport_pages->setCurrentIndex(1);
    }
    else
    {
        this->unknown_transport_label->setText(
            tr("transport_t '%1' is preserved from JSON, but this UI does not yet provide a "
               "dedicated editor for it.")
                .arg(normalized.isEmpty() ? tr("(empty)") : normalized));
        this->transport_pages->setCurrentIndex(2);
    }

    this->update_current_http_status_ui();
}

void mcp_servers_widget_t::update_current_http_status_ui()
{
    const qtmcp::server_definition_t *server = this->current_server();
    const bool isHttpServer =
        server != nullptr && server->transport.trimmed() == QStringLiteral("http");

    if (isHttpServer == false)
    {
        this->http_status_label->setText(notTestedText());
        this->http_status_label->setStyleSheet(QString());
        this->test_connection_button->setEnabled(false);
        this->authorize_button->setEnabled(false);
        return;
    }

    const http_status_t status = this->http_statuses.value(this->current_server_name());
    QString text = status.message;
    if (text.isEmpty() == true)
    {
        text = notTestedText();
    }

    this->http_status_label->setText(text);
    const QColor color = statusColor(status.kind, this);
    this->http_status_label->setStyleSheet(
        QStringLiteral("color: %1;").arg(color.name(QColor::HexRgb)));

    const bool canTest = !this->authorizing && !this->testing_connection && server->enabled;
    const bool canAuthorize = status.kind == http_status_kind_t::AUTHORIZATION_REQUIRED &&
                              !this->authorizing && !this->testing_connection;
    this->test_connection_button->setEnabled(canTest);
    this->authorize_button->setEnabled(canAuthorize);
}

void mcp_servers_widget_t::update_server_list_item(const QString &serverName)
{
    for (int i = 0; ((i < this->server_list->count()) == true); ++i)
    {
        QListWidgetItem *item = this->server_list->item(i);
        if (((item == nullptr || item->data(Qt::UserRole).toString() != serverName) == true))
        {
            continue;
        }

        const http_status_t status = this->http_statuses.value(serverName);
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

void mcp_servers_widget_t::set_http_status(const QString &serverName, http_status_kind_t kind,
                                           const QString &message)
{
    http_status_t status;
    status.kind = kind;
    status.message = message;
    this->http_statuses.insert(serverName, status);
    this->persist_connection_states();
    this->update_server_list_item(serverName);
    if (((serverName == this->current_server_name()) == true))
    {
        this->update_current_http_status_ui();
    }
}

void mcp_servers_widget_t::clear_http_status(const QString &serverName)
{
    if (serverName.isEmpty() == true)
    {
        return;
    }

    this->http_statuses.remove(serverName);
    this->authorized_servers.remove(serverName);
    this->persist_connection_states();
    this->update_server_list_item(serverName);
    if (((serverName == this->current_server_name()) == true))
    {
        this->update_current_http_status_ui();
    }
}

void mcp_servers_widget_t::load_persisted_connection_states()
{
    QString error;
    const mcp_server_connection_states_t storedStates = load_mcp_server_connection_states(&error);
    if (error.isEmpty() == false)
    {
        QCAI_WARN("settings_t", error);
    }

    for (auto it = storedStates.cbegin(); it != storedStates.cend(); ++it)
    {
        if (((!this->server_definitions.contains(it.key())) == true))
        {
            continue;
        }

        http_status_t status;
        const QString state = it.value().state.trimmed();
        if (((state == QStringLiteral("authorized")) == true))
        {
            status.kind = http_status_kind_t::REACHABLE;
            this->authorized_servers.insert(it.key());
            if (it.value().message.trimmed().isEmpty())
            {
                status.message = authorizedText();
            }
        }
        else if (((state == QStringLiteral("reachable")) == true))
        {
            status.kind = http_status_kind_t::REACHABLE;
        }
        else if (((state == QStringLiteral("authorization_required")) == true))
        {
            status.kind = http_status_kind_t::AUTHORIZATION_REQUIRED;
        }
        else if (((state == QStringLiteral("error")) == true))
        {
            status.kind = http_status_kind_t::ERROR;
        }
        else
        {
            continue;
        }
        if (status.message.isEmpty())
        {
            status.message = it.value().message.trimmed();
        }
        this->http_statuses.insert(it.key(), status);
        if (status.kind == http_status_kind_t::REACHABLE &&
            (status.message == authorizedText() ||
             status.message == QStringLiteral("Авторизовано")))
        {
            this->authorized_servers.insert(it.key());
        }
    }

    this->persist_connection_states();
}

void mcp_servers_widget_t::persist_connection_states() const
{
    mcp_server_connection_states_t states;
    for (auto it = this->http_statuses.cbegin(); it != this->http_statuses.cend(); ++it)
    {
        if (!this->server_definitions.contains(it.key()))
        {
            continue;
        }

        mcp_server_connection_state_t state;
        switch (it.value().kind)
        {
            case http_status_kind_t::CHECKING:
                continue;
            case http_status_kind_t::REACHABLE:
                state.state = this->authorized_servers.contains(it.key())
                                  ? QStringLiteral("authorized")
                                  : QStringLiteral("reachable");
                break;
            case http_status_kind_t::AUTHORIZATION_REQUIRED:
                state.state = QStringLiteral("authorization_required");
                break;
            case http_status_kind_t::ERROR:
                state.state = QStringLiteral("error");
                break;
            case http_status_kind_t::UNKNOWN:
                continue;
        }
        state.message = it.value().message;
        states.insert(it.key(), state);
    }

    QString error;
    if (!save_mcp_server_connection_states(states, &error) && !error.isEmpty())
    {
        QCAI_WARN("settings_t", error);
    }
}

void mcp_servers_widget_t::finish_connection_test(const QString &serverName,
                                                  http_status_kind_t kind, const QString &message,
                                                  bool authorized)
{
    if (authorized)
    {
        this->authorized_servers.insert(serverName);
    }
    else if (kind != http_status_kind_t::REACHABLE || message != authorizedText())
    {
        this->authorized_servers.remove(serverName);
    }

    this->set_http_status(serverName, kind, message);
    this->cleanup_connection_test();
}

void mcp_servers_widget_t::cleanup_connection_test()
{
    if (this->connection_test_timer != nullptr)
    {
        this->connection_test_timer->stop();
    }

    qtmcp::client_t *client = this->connection_test_client;
    this->connection_test_client = nullptr;
    this->connection_test_server_name.clear();
    this->connection_test_request_id = 0;
    this->testing_connection = false;

    if (client != nullptr)
    {
        client->disconnect(this);
        client->stop();
        client->deleteLater();
    }

    this->update_current_http_status_ui();
}

void mcp_servers_widget_t::invalidate_current_server_status()
{
    const QString serverName = this->current_server_name();
    if (serverName.isEmpty())
    {
        return;
    }

    if (this->testing_connection && this->connection_test_server_name == serverName)
    {
        this->cleanup_connection_test();
    }
    this->clear_http_status(serverName);
    emit this->changed();
}

void mcp_servers_widget_t::test_current_server_connection()
{
    if (this->testing_connection || this->authorizing)
    {
        return;
    }

    const QString serverName = this->current_server_name();
    const qtmcp::server_definition_t *definition = this->current_server();
    if (serverName.isEmpty() || definition == nullptr ||
        definition->transport.trimmed() != QStringLiteral("http"))
    {
        return;
    }

    if (!definition->enabled)
    {
        this->set_http_status(serverName, http_status_kind_t::ERROR,
                              tr("Error: server is disabled"));
        return;
    }

    if (definition->url.trimmed().isEmpty())
    {
        this->set_http_status(serverName, http_status_kind_t::ERROR, tr("Error: URL is not set"));
        return;
    }

    this->testing_connection = true;
    this->connection_test_server_name = serverName;
    this->connection_test_request_id = 0;
    this->set_http_status(serverName, http_status_kind_t::CHECKING, checkingText());
    const int startup_timeout_ms = probe_timeout_ms(definition->startup_timeout_ms);
    const int request_timeout_ms = probe_timeout_ms(definition->request_timeout_ms);

    qtmcp::http_transport_config_t transportConfig =
        buildHttpTransportConfig(serverName, *definition);
    transportConfig.interactive_o_auth_enabled = false;

    auto *client = new qtmcp::client_t(this);
    client->set_transport(std::make_unique<qtmcp::http_transport_t>(transportConfig));
    this->connection_test_client = client;

    auto *transport = qobject_cast<qtmcp::http_transport_t *>(client->transport());
    Q_ASSERT(transport != nullptr);

    connect(client, &qtmcp::client_t::transport_log_message, this,
            [serverName](const QString &message) { logMcpSettingsDebug(serverName, message); });
    connect(client, &qtmcp::client_t::transport_error_occurred, this,
            [this, client, transport, serverName](const QString &message) {
                if (client != this->connection_test_client)
                {
                    return;
                }
                QCAI_ERROR("MCP",
                           QStringLiteral("[settings:%1] %2").arg(serverName, message.trimmed()));

                const QString transportOAuthError = transport->last_o_auth_error().trimmed();
                if (transport->last_authorization_required() ||
                    transportOAuthError == QStringLiteral("OAuth authorization required.") ||
                    message.trimmed() == QStringLiteral("OAuth authorization required."))
                {
                    this->finish_connection_test(serverName,
                                                 http_status_kind_t::AUTHORIZATION_REQUIRED,
                                                 authorizationRequiredText());
                    return;
                }

                const QString details =
                    !transportOAuthError.isEmpty() ? transportOAuthError : message.trimmed();
                this->finish_connection_test(serverName, http_status_kind_t::ERROR,
                                             details.isEmpty() ? errorPrefixText()
                                                               : tr("Error: %1").arg(details));
            });

    connect(
        client, &qtmcp::client_t::connected, this,
        [this, client, transport, serverName, request_timeout_ms]() {
            if (client != this->connection_test_client)
            {
                return;
            }

            if (this->connection_test_timer != nullptr)
            {
                this->connection_test_timer->start(request_timeout_ms);
            }

            if (transport->config().oauth_enabled &&
                this->authorized_servers.contains(serverName) &&
                transport->has_cached_o_auth_credentials())
            {
                logMcpSettingsDebug(
                    serverName,
                    QStringLiteral(
                        "Skipping redundant HTTP initialize probe because this server is already "
                        "authorized and cached OAuth credentials are available."));
                this->finish_connection_test(serverName, http_status_kind_t::REACHABLE,
                                             authorizedText(), true);
                return;
            }

            if (transport->config().oauth_enabled && !transport->has_cached_o_auth_credentials())
            {
                logMcpSettingsDebug(
                    serverName,
                    QStringLiteral("Skipping HTTP initialize probe because no cached OAuth "
                                   "credentials are available; authorization is required first."));
                this->finish_connection_test(serverName,
                                             http_status_kind_t::AUTHORIZATION_REQUIRED,
                                             authorizationRequiredText());
                return;
            }

            const qint64 requestId =
                client->send_request(QStringLiteral("initialize"), initializeParamsObject());
            if (requestId <= 0)
            {
                this->finish_connection_test(serverName, http_status_kind_t::ERROR,
                                             tr("Error: failed to send initialize request"));
                return;
            }
            this->connection_test_request_id = requestId;
        });

    connect(client, &qtmcp::client_t::response_received, this,
            [this, client, transport, serverName](const QJsonValue &id, const QJsonValue &) {
                if (client != this->connection_test_client ||
                    id.toInteger() != this->connection_test_request_id)
                {
                    return;
                }
                this->finish_connection_test(serverName, http_status_kind_t::REACHABLE,
                                             transport->config().oauth_enabled ? authorizedText()
                                                                               : connectedText(),
                                             transport->config().oauth_enabled);
            });

    connect(client, &qtmcp::client_t::error_response_received, this,
            [this, client, transport, serverName](const QJsonValue &id, int,
                                                  const QString &message, const QJsonValue &) {
                if (client != this->connection_test_client ||
                    id.toInteger() != this->connection_test_request_id)
                {
                    return;
                }

                const QString transportOAuthError = transport->last_o_auth_error().trimmed();
                if (transport->last_authorization_required() ||
                    transportOAuthError == QStringLiteral("OAuth authorization required."))
                {
                    this->finish_connection_test(serverName,
                                                 http_status_kind_t::AUTHORIZATION_REQUIRED,
                                                 authorizationRequiredText());
                    return;
                }

                const QString details =
                    !transportOAuthError.isEmpty() ? transportOAuthError : message.trimmed();
                this->finish_connection_test(serverName, http_status_kind_t::ERROR,
                                             details.isEmpty() ? errorPrefixText()
                                                               : tr("Error: %1").arg(details));
            });

    connect(client, &qtmcp::client_t::disconnected, this, [this, client, serverName]() {
        if (client != this->connection_test_client)
        {
            return;
        }
        this->finish_connection_test(serverName, http_status_kind_t::ERROR,
                                     tr("Error: MCP server disconnected"));
    });

    if (this->connection_test_timer != nullptr)
    {
        this->connection_test_timer->start(startup_timeout_ms);
    }
    client->start();
}

void mcp_servers_widget_t::authorize_current_server()
{
    if (this->authorizing || this->testing_connection)
    {
        return;
    }

    const QString serverName = this->current_server_name();
    const qtmcp::server_definition_t *definition = this->current_server();
    if (serverName.isEmpty() || definition == nullptr ||
        definition->transport.trimmed() != QStringLiteral("http"))
    {
        return;
    }

    this->authorizing = true;
    this->set_http_status(serverName, http_status_kind_t::CHECKING, authorizingText());
    this->update_current_http_status_ui();

    qtmcp::http_transport_config_t transportConfig =
        buildHttpTransportConfig(serverName, *definition);
    transportConfig.oauth_enabled = true;

    qtmcp::http_transport_t transport(std::move(transportConfig));
    connect(&transport, &qtmcp::transport_t::log_message, this,
            [serverName](const QString &message) { logMcpSettingsDebug(serverName, message); });
    connect(&transport, &qtmcp::transport_t::error_occurred, this,
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

    this->authorizing = false;
    if (!authorized)
    {
        this->authorized_servers.remove(serverName);
        this->set_http_status(serverName, http_status_kind_t::AUTHORIZATION_REQUIRED,
                              authorizationRequiredText());
        if (!errorMessage.trimmed().isEmpty())
        {
            QMessageBox::warning(this, tr("OAuth Authorization Failed"), errorMessage.trimmed());
        }
        this->update_current_http_status_ui();
        return;
    }

    this->authorized_servers.insert(serverName);
    this->set_http_status(serverName, http_status_kind_t::REACHABLE, authorizedText());
    this->update_current_http_status_ui();
}

QString mcp_servers_widget_t::make_unique_server_name(const QString &baseName) const
{
    QString candidate = baseName;
    int suffix = 2;
    while (this->server_definitions.contains(candidate))
    {
        candidate = QStringLiteral("%1-%2").arg(baseName).arg(suffix++);
    }
    return candidate;
}

void mcp_servers_widget_t::rename_current_server()
{
    if (this->updating)
    {
        return;
    }

    const QString oldName = this->current_server_name();
    if (oldName.isEmpty())
    {
        return;
    }

    if (this->testing_connection && this->connection_test_server_name == oldName)
    {
        this->cleanup_connection_test();
    }

    const QString newName = this->name_edit->text().trimmed();
    if (newName.isEmpty())
    {
        QMessageBox::warning(this, tr("Invalid MCP Server Name"),
                             tr("The MCP server name must not be empty."));
        this->updating = true;
        this->name_edit->setText(oldName);
        this->updating = false;
        return;
    }

    if (newName != oldName && this->server_definitions.contains(newName))
    {
        QMessageBox::warning(this, tr("Duplicate MCP Server Name"),
                             tr("An MCP server named '%1' already exists.").arg(newName));
        this->updating = true;
        this->name_edit->setText(oldName);
        this->updating = false;
        return;
    }

    if (newName == oldName)
    {
        return;
    }

    const http_status_t preservedStatus = this->http_statuses.take(oldName);
    const bool wasAuthorized = this->authorized_servers.remove(oldName);
    const qtmcp::server_definition_t definition = this->server_definitions.take(oldName);
    this->server_definitions.insert(newName, definition);
    if (preservedStatus.kind != http_status_kind_t::UNKNOWN || !preservedStatus.message.isEmpty())
    {
        this->http_statuses.insert(newName, preservedStatus);
    }
    if (wasAuthorized)
    {
        this->authorized_servers.insert(newName);
    }
    this->persist_connection_states();
    this->repopulate_server_list(newName);
    emit this->changed();
}

QStringList mcp_servers_widget_t::text_lines(const QString &text)
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

QStringList mcp_servers_widget_t::join_map_lines(const QMap<QString, QString> &values,
                                                 QChar separator, bool insertSpaceAfterSeparator)
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

QMap<QString, QString> mcp_servers_widget_t::parse_map_lines(const QString &text, QChar separator)
{
    QMap<QString, QString> values;
    for (const QString &line : text_lines(text))
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
