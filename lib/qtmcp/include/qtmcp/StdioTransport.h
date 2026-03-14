/*! Declares the stdio transport implementation for the Qt MCP client. */
#pragma once

#include <qtmcp/Transport.h>

#include <QByteArray>
#include <QProcessEnvironment>
#include <QStringList>

class QProcess;

namespace qtmcp
{

struct StdioTransportConfig
{
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
};

class StdioTransport final : public Transport
{
    Q_OBJECT

public:
    explicit StdioTransport(StdioTransportConfig config, QObject *parent = nullptr);
    ~StdioTransport() override;

    QString transportName() const override;
    State state() const override;
    const StdioTransportConfig &config() const;

    void start() override;
    void stop() override;
    bool sendMessage(const QJsonObject &message) override;

private:
    void setState(State state);
    void consumeStdout();
    void consumeStderr();
    void processBufferedMessages();

    StdioTransportConfig m_config;
    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    State m_state = State::Disconnected;
};

}  // namespace qtmcp
