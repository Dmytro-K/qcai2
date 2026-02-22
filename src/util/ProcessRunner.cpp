#include "ProcessRunner.h"
#include "Logger.h"

#include <QProcess>
#include <QTimer>
#include <QEventLoop>

namespace Qcai2 {

ProcessRunner::ProcessRunner(QObject *parent) : QObject(parent) {}

ProcessRunner::Result ProcessRunner::run(const QString &program,
                                          const QStringList &args,
                                          const QString &workDir,
                                          int timeoutMs,
                                          const QString &stdinData)
{
    QProcess proc;
    if (!workDir.isEmpty())
        proc.setWorkingDirectory(workDir);
    proc.setProgram(program);
    proc.setArguments(args);

    proc.start();
    QCAI_DEBUG("Process", QStringLiteral("Running: %1 %2 (workDir: %3, timeout: %4ms)")
        .arg(program, args.join(QLatin1Char(' ')), workDir).arg(timeoutMs));
    if (!stdinData.isEmpty()) {
        proc.write(stdinData.toUtf8());
        proc.closeWriteChannel();
    }

    Result res;
    bool finished = proc.waitForFinished(timeoutMs);
    if (!finished) {
        proc.kill();
        QCAI_WARN("Process", QStringLiteral("Process timed out: %1").arg(program));
        res.success     = false;
        res.errorString = QStringLiteral("Process timed out after %1 ms").arg(timeoutMs);
        res.stdErr      = QString::fromUtf8(proc.readAllStandardError());
        return res;
    }

    res.exitCode   = proc.exitCode();
    res.success    = (proc.exitStatus() == QProcess::NormalExit) && (res.exitCode == 0);
    res.stdOut     = QString::fromUtf8(proc.readAllStandardOutput());
    res.stdErr     = QString::fromUtf8(proc.readAllStandardError());
    res.errorString = proc.errorString();
    QCAI_DEBUG("Process", QStringLiteral("Process finished: %1 exitCode=%2 success=%3")
        .arg(program).arg(res.exitCode).arg(res.success ? QStringLiteral("yes") : QStringLiteral("no")));
    return res;
}

void ProcessRunner::runAsync(const QString &program,
                              const QStringList &args,
                              const QString &workDir,
                              int timeoutMs)
{
    auto *proc = new QProcess(this);
    if (!workDir.isEmpty())
        proc->setWorkingDirectory(workDir);

    // Stream stdout lines
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        handleReadyRead();
        Q_UNUSED(proc)
    });

    // Timeout guard
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, proc, [proc]() { proc->kill(); });
    timer->start(timeoutMs);

    connect(proc, &QProcess::finished, this, [this, proc, timer](int exitCode, QProcess::ExitStatus status) {
        timer->stop();
        timer->deleteLater();

        Result res;
        res.exitCode   = exitCode;
        res.success    = (status == QProcess::NormalExit) && (exitCode == 0);
        res.stdOut     = QString::fromUtf8(proc->readAllStandardOutput());
        res.stdErr     = QString::fromUtf8(proc->readAllStandardError());
        res.errorString = proc->errorString();
        proc->deleteLater();
        emit finished(res);
    });

    proc->start(program, args);
}

void ProcessRunner::handleReadyRead()
{
    // Not wired to a specific process here; streaming is handled in runAsync lambda above
}

} // namespace Qcai2
