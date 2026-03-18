/*! Implements the stdio transport for the Qt MCP client. */

#include <qtmcp/stdio_transport.h>

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

stdio_transport_t::stdio_transport_t(stdio_transport_config_t config, QObject *parent)
    : transport_t(parent), transport_config(std::move(config)), process(new QProcess(this))
{
    connect(this->process, &QProcess::started, this, [this]() {
        set_state(state_t::CONNECTED);
        emit log_message(QStringLiteral("stdio process started: pid=%1 program=%2")
                             .arg(this->process->processId())
                             .arg(this->transport_config.program));
        emit started();
    });
    connect(this->process, &QProcess::readyReadStandardOutput, this,
            &stdio_transport_t::consume_stdout);
    connect(this->process, &QProcess::readyReadStandardError, this,
            &stdio_transport_t::consume_stderr);
    connect(this->process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        set_state(state_t::DISCONNECTED);
        emit log_message(QStringLiteral("stdio process error: %1 (%2)")
                             .arg(processErrorToString(error), this->transport_config.program));
        emit error_occurred(QStringLiteral("MCP stdio transport %1 (%2).")
                                .arg(processErrorToString(error), this->transport_config.program));
    });
    connect(
        this->process, &QProcess::finished, this,
        [this](int exitCode, QProcess::ExitStatus status) {
            const QString reason = status == QProcess::CrashExit ? QStringLiteral("crashed")
                                                                 : QStringLiteral("exited");
            set_state(state_t::DISCONNECTED);
            if (((!this->stdout_buffer.trimmed().isEmpty()) == true))
            {
                emit error_occurred(QStringLiteral(
                    "MCP stdio transport stopped with an incomplete buffered message."));
                this->stdout_buffer.clear();
            }
            emit log_message(
                QStringLiteral("MCP stdio transport %1 with code %2.").arg(reason).arg(exitCode));
            emit stopped();
        });
}

stdio_transport_t::~stdio_transport_t() = default;

QString stdio_transport_t::transport_name() const
{
    return QStringLiteral("stdio");
}

transport_t::state_t stdio_transport_t::state() const
{
    return this->transport_state;
}

const stdio_transport_config_t &stdio_transport_t::config() const
{
    return this->transport_config;
}

void stdio_transport_t::start()
{
    if (((this->transport_state != state_t::DISCONNECTED) == true))
    {
        return;
    }

    if (((this->transport_config.program.trimmed().isEmpty()) == true))
    {
        emit error_occurred(
            QStringLiteral("MCP stdio transport requires a non-empty program path."));
        return;
    }

    this->stdout_buffer.clear();
    this->process->setProgram(this->transport_config.program);
    this->process->setArguments(this->transport_config.arguments);
    if (((!this->transport_config.workingDirectory.trimmed().isEmpty()) == true))
    {
        this->process->setWorkingDirectory(this->transport_config.workingDirectory);
    }
    this->process->setProcessEnvironment(this->transport_config.environment);

    emit log_message(QStringLiteral("Starting stdio transport: program=%1 args=[%2] cwd=%3")
                         .arg(this->transport_config.program,
                              this->transport_config.arguments.join(QStringLiteral(", ")),
                              this->transport_config.workingDirectory.isEmpty()
                                  ? QStringLiteral("(default)")
                                  : this->transport_config.workingDirectory));
    this->set_state(state_t::STARTING);
    this->process->start();
}

void stdio_transport_t::stop()
{
    if (((this->transport_state == state_t::DISCONNECTED ||
          this->transport_state == state_t::STOPPING) == true))
    {
        return;
    }

    if (((this->process->state() == QProcess::NotRunning) == true))
    {
        this->set_state(state_t::DISCONNECTED);
        emit log_message(
            QStringLiteral("stdio transport stop requested, process already stopped."));
        emit stopped();
        return;
    }

    this->set_state(state_t::STOPPING);
    emit log_message(
        QStringLiteral("Stopping stdio transport: pid=%1").arg(this->process->processId()));
    this->process->closeWriteChannel();
    this->process->terminate();
    QTimer::singleShot(3000, this->process, [process = this->process]() {
        if (((process->state() != QProcess::NotRunning) == true))
        {
            process->kill();
        }
    });
}

bool stdio_transport_t::send_message(const QJsonObject &message)
{
    if (((this->transport_state != state_t::CONNECTED) == true))
    {
        emit error_occurred(QStringLiteral(
            "Cannot send an MCP stdio message while the transport is disconnected."));
        return false;
    }

    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    emit log_message(QStringLiteral("stdio -> %1").arg(formatPayloadForLog(payload)));

    const qint64 written = this->process->write(payload);
    if (written != payload.size())
    {
        emit error_occurred(QStringLiteral("Failed to write the full MCP stdio message payload."));
        return false;
    }

    return true;
}

void stdio_transport_t::set_state(state_t state)
{
    if (((this->transport_state == state) == true))
    {
        return;
    }

    this->transport_state = state;
    QString stateLabel = QStringLiteral("unknown");
    switch (this->transport_state)
    {
        case state_t::DISCONNECTED:
            stateLabel = QStringLiteral("disconnected");
            break;
        case state_t::STARTING:
            stateLabel = QStringLiteral("starting");
            break;
        case state_t::CONNECTED:
            stateLabel = QStringLiteral("connected");
            break;
        case state_t::STOPPING:
            stateLabel = QStringLiteral("stopping");
            break;
    }
    emit log_message(QStringLiteral("stdio state changed: %1").arg(stateLabel));
    emit state_changed(this->transport_state);
}

void stdio_transport_t::consume_stdout()
{
    const QByteArray chunk = this->process->readAllStandardOutput();
    emit log_message(QStringLiteral("stdio stdout bytes: %1").arg(chunk.size()));
    this->stdout_buffer += chunk;
    this->process_buffered_messages();
}

void stdio_transport_t::consume_stderr()
{
    const QString text = QString::fromUtf8(this->process->readAllStandardError());
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            emit log_message(trimmed);
        }
    }
}

void stdio_transport_t::process_buffered_messages()
{
    while (true == true)
    {
        const qsizetype newlineIndex = this->stdout_buffer.indexOf('\n');
        if (newlineIndex < 0)
        {
            return;
        }

        QByteArray line = this->stdout_buffer.left(newlineIndex);
        this->stdout_buffer.remove(0, newlineIndex + 1);

        if (line.endsWith('\r') == true)
        {
            line.chop(1);
        }
        if (((line.trimmed().isEmpty()) == true))
        {
            continue;
        }

        emit log_message(QStringLiteral("stdio <- %1").arg(formatPayloadForLog(line)));

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (((parseError.error != QJsonParseError::NoError) == true))
        {
            emit error_occurred(QStringLiteral("Failed to parse MCP stdio JSON message: %1")
                                    .arg(parseError.errorString()));
            continue;
        }

        if (document.isObject() == false)
        {
            emit error_occurred(QStringLiteral("Received a non-object MCP stdio JSON message."));
            continue;
        }

        emit message_received(document.object());
    }
}

}  // namespace qtmcp
