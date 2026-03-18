/*! Declares the stdio transport implementation for the Qt MCP client. */
#pragma once

#include <qtmcp/Transport.h>

#include <QByteArray>
#include <QProcessEnvironment>
#include <QStringList>

class QProcess;

namespace qtmcp
{

struct stdio_transport_config_t
{
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
};

class stdio_transport_t final : public transport_t
{
    Q_OBJECT

public:
    explicit stdio_transport_t(stdio_transport_config_t config, QObject *parent = nullptr);
    ~stdio_transport_t() override;

    QString transport_name() const override;
    state_t state() const override;
    const stdio_transport_config_t &config() const;

    void start() override;
    void stop() override;
    bool send_message(const QJsonObject &message) override;

private:
    void set_state(state_t state);
    void consume_stdout();
    void consume_stderr();
    void process_buffered_messages();

    stdio_transport_config_t transport_config;
    QProcess *process = nullptr;
    QByteArray stdout_buffer;
    state_t transport_state = state_t::DISCONNECTED;
};

}  // namespace qtmcp
