/*! Declares the settings widget used to edit global MCP server definitions. */
#pragma once

#include <qtmcp/server_definition.h>

#include <QSet>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace qtmcp
{
class client_t;
}

namespace qcai2
{

class settings_spin_box_t;

class mcp_servers_widget_t final : public QWidget
{
    Q_OBJECT

public:
    enum class http_status_kind_t
    {
        UNKNOWN,
        CHECKING,
        REACHABLE,
        AUTHORIZATION_REQUIRED,
        ERROR
    };

    struct http_status_t
    {
        http_status_kind_t kind = http_status_kind_t::UNKNOWN;
        QString message;
    };

    explicit mcp_servers_widget_t(QWidget *parent = nullptr);

    void set_servers(const qtmcp::server_definitions_t &servers);
    qtmcp::server_definitions_t servers() const;

signals:
    void changed();

private:
    QString current_server_name() const;
    qtmcp::server_definition_t *current_server();
    const qtmcp::server_definition_t *current_server() const;

    void repopulate_server_list(const QString &selectedServer = {});
    void load_current_server();
    void clear_editors();
    void set_editors_enabled(bool enabled);
    void update_transport_page(const QString &transport);
    void update_current_http_status_ui();
    void update_server_list_item(const QString &serverName);
    void set_http_status(const QString &serverName, http_status_kind_t kind,
                         const QString &message);
    void clear_http_status(const QString &serverName);
    void load_persisted_connection_states();
    void persist_connection_states() const;
    void finish_connection_test(const QString &serverName, http_status_kind_t kind,
                                const QString &message, bool authorized = false);
    void cleanup_connection_test();
    void invalidate_current_server_status();
    void test_current_server_connection();
    void authorize_current_server();
    QString make_unique_server_name(const QString &baseName) const;
    void rename_current_server();

    static QStringList text_lines(const QString &text);
    static QStringList join_map_lines(const QMap<QString, QString> &values, QChar separator,
                                      bool insertSpaceAfterSeparator);
    static QMap<QString, QString> parse_map_lines(const QString &text, QChar separator);

    qtmcp::server_definitions_t server_definitions;
    bool updating = false;

    QListWidget *server_list = nullptr;
    QPushButton *add_button = nullptr;
    QPushButton *remove_button = nullptr;
    QLineEdit *name_edit = nullptr;
    QCheckBox *enabled_check = nullptr;
    QComboBox *transport_combo = nullptr;
    QStackedWidget *transport_pages = nullptr;
    QLabel *unknown_transport_label = nullptr;
    QLineEdit *command_edit = nullptr;
    QPlainTextEdit *args_edit = nullptr;
    QPlainTextEdit *env_edit = nullptr;
    QLineEdit *url_edit = nullptr;
    QLineEdit *token_edit = nullptr;
    QPlainTextEdit *headers_edit = nullptr;
    QLabel *http_status_label = nullptr;
    QPushButton *test_connection_button = nullptr;
    QPushButton *authorize_button = nullptr;
    settings_spin_box_t *startup_timeout_spin = nullptr;
    settings_spin_box_t *request_timeout_spin = nullptr;
    QTimer *connection_test_timer = nullptr;
    qtmcp::client_t *connection_test_client = nullptr;
    QString connection_test_server_name;
    qint64 connection_test_request_id = 0;
    QMap<QString, http_status_t> http_statuses;
    QSet<QString> authorized_servers;
    bool authorizing = false;
    bool testing_connection = false;
};

}  // namespace qcai2
