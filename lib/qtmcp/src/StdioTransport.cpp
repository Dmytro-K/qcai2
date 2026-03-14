/*! Implements the stdio transport for the Qt MCP client. */

#include <qtmcp/StdioTransport.h>

#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QTimer>

#include <utility>

namespace qtmcp
{

namespace
{

constexpr int kMaxLoggedPayloadChars = 2000;

QString processErrorToString(QProcess::ProcessError error)
{
    switch (error)
    {
        case QProcess::FailedToStart:
            return QStringLiteral("failed to start");
        case QProcess::Crashed:
            return QStringLiteral("crashed");
        case QProcess::Timedout:
            return QStringLiteral("timed out");
        case QProcess::WriteError:
            return QStringLiteral("write error");
        case QProcess::ReadError:
            return QStringLiteral("read error");
        case QProcess::UnknownError:
            break;
    }

    return QStringLiteral("unknown process error");
}

QString formatPayloadForLog(const QByteArray &payload)
{
    QString text = QString::fromUtf8(payload);
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    if (text.size() > kMaxLoggedPayloadChars)
    {
        text = text.left(kMaxLoggedPayloadChars);
        text += QStringLiteral("... [truncated]");
    }
    return text;
}

}  // namespace

StdioTransport::StdioTransport(StdioTransportConfig config, QObject *parent)
    : Transport(parent), m_config(std::move(config)), m_process(new QProcess(this))
{
    connect(m_process, &QProcess::started, this, [this]() {
        setState(State::Connected);
        emit logMessage(QStringLiteral("stdio process started: pid=%1 program=%2")
                            .arg(m_process->processId())
                            .arg(m_config.program));
        emit started();
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, &StdioTransport::consumeStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &StdioTransport::consumeStderr);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        setState(State::Disconnected);
        emit logMessage(QStringLiteral("stdio process error: %1 (%2)")
                            .arg(processErrorToString(error), m_config.program));
        emit errorOccurred(QStringLiteral("MCP stdio transport %1 (%2).")
                               .arg(processErrorToString(error), m_config.program));
    });
    connect(
        m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
            const QString reason = status == QProcess::CrashExit ? QStringLiteral("crashed")
                                                                 : QStringLiteral("exited");
            setState(State::Disconnected);
            if (!m_stdoutBuffer.trimmed().isEmpty())
            {
                emit errorOccurred(QStringLiteral(
                    "MCP stdio transport stopped with an incomplete buffered message."));
                m_stdoutBuffer.clear();
            }
            emit logMessage(
                QStringLiteral("MCP stdio transport %1 with code %2.").arg(reason).arg(exitCode));
            emit stopped();
        });
}

StdioTransport::~StdioTransport() = default;

QString StdioTransport::transportName() const
{
    return QStringLiteral("stdio");
}

Transport::State StdioTransport::state() const
{
    return m_state;
}

const StdioTransportConfig &StdioTransport::config() const
{
    return m_config;
}

void StdioTransport::start()
{
    if (m_state != State::Disconnected)
        return;

    if (m_config.program.trimmed().isEmpty())
    {
        emit errorOccurred(
            QStringLiteral("MCP stdio transport requires a non-empty program path."));
        return;
    }

    m_stdoutBuffer.clear();
    m_process->setProgram(m_config.program);
    m_process->setArguments(m_config.arguments);
    if (!m_config.workingDirectory.trimmed().isEmpty())
        m_process->setWorkingDirectory(m_config.workingDirectory);
    m_process->setProcessEnvironment(m_config.environment);

    emit logMessage(QStringLiteral("Starting stdio transport: program=%1 args=[%2] cwd=%3")
                        .arg(m_config.program, m_config.arguments.join(QStringLiteral(", ")),
                             m_config.workingDirectory.isEmpty() ? QStringLiteral("(default)")
                                                                 : m_config.workingDirectory));
    setState(State::Starting);
    m_process->start();
}

void StdioTransport::stop()
{
    if (m_state == State::Disconnected || m_state == State::Stopping)
        return;

    if (m_process->state() == QProcess::NotRunning)
    {
        setState(State::Disconnected);
        emit logMessage(QStringLiteral("stdio transport stop requested, process already stopped."));
        emit stopped();
        return;
    }

    setState(State::Stopping);
    emit logMessage(QStringLiteral("Stopping stdio transport: pid=%1").arg(m_process->processId()));
    m_process->closeWriteChannel();
    m_process->terminate();
    QTimer::singleShot(3000, m_process, [process = m_process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

bool StdioTransport::sendMessage(const QJsonObject &message)
{
    if (m_state != State::Connected)
    {
        emit errorOccurred(QStringLiteral(
            "Cannot send an MCP stdio message while the transport is disconnected."));
        return false;
    }

    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    emit logMessage(QStringLiteral("stdio -> %1").arg(formatPayloadForLog(payload)));

    const qint64 written = m_process->write(payload);
    if (written != payload.size())
    {
        emit errorOccurred(QStringLiteral("Failed to write the full MCP stdio message payload."));
        return false;
    }

    return true;
}

void StdioTransport::setState(State state)
{
    if (m_state == state)
        return;

    m_state = state;
    QString stateLabel = QStringLiteral("unknown");
    switch (m_state)
    {
        case State::Disconnected:
            stateLabel = QStringLiteral("disconnected");
            break;
        case State::Starting:
            stateLabel = QStringLiteral("starting");
            break;
        case State::Connected:
            stateLabel = QStringLiteral("connected");
            break;
        case State::Stopping:
            stateLabel = QStringLiteral("stopping");
            break;
    }
    emit logMessage(QStringLiteral("stdio state changed: %1").arg(stateLabel));
    emit stateChanged(m_state);
}

void StdioTransport::consumeStdout()
{
    const QByteArray chunk = m_process->readAllStandardOutput();
    emit logMessage(QStringLiteral("stdio stdout bytes: %1").arg(chunk.size()));
    m_stdoutBuffer += chunk;
    processBufferedMessages();
}

void StdioTransport::consumeStderr()
{
    const QString text = QString::fromUtf8(m_process->readAllStandardError());
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            emit logMessage(trimmed);
    }
}

void StdioTransport::processBufferedMessages()
{
    while (true)
    {
        const qsizetype newlineIndex = m_stdoutBuffer.indexOf('\n');
        if (newlineIndex < 0)
            return;

        QByteArray line = m_stdoutBuffer.left(newlineIndex);
        m_stdoutBuffer.remove(0, newlineIndex + 1);

        if (line.endsWith('\r'))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;

        emit logMessage(QStringLiteral("stdio <- %1").arg(formatPayloadForLog(line)));

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            emit errorOccurred(QStringLiteral("Failed to parse MCP stdio JSON message: %1")
                                   .arg(parseError.errorString()));
            continue;
        }

        if (!document.isObject())
        {
            emit errorOccurred(QStringLiteral("Received a non-object MCP stdio JSON message."));
            continue;
        }

        emit messageReceived(document.object());
    }
}

}  // namespace qtmcp
