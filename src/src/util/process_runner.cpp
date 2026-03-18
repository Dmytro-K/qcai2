/*! @file
    @brief Implements synchronous and asynchronous process execution helpers.
*/

#include "process_runner.h"
#include "logger.h"

#include <QEventLoop>
#include <QProcess>
#include <QTimer>

namespace qcai2
{

/**
 * @brief Creates a process runner bound to an optional QObject parent.
 * @param parent Parent QObject that owns this instance.
 */
process_runner_t::process_runner_t(QObject *parent) : QObject(parent)
{
}

process_runner_t::result_t process_runner_t::run(const QString &program, const QStringList &args,
                                                 const QString &workDir, int timeoutMs,
                                                 const QString &stdinData)
{
    QProcess proc;
    if (workDir.isEmpty() == false)
    {
        proc.setWorkingDirectory(workDir);
    }
    proc.setProgram(program);
    proc.setArguments(args);

    proc.start();
    QCAI_DEBUG("Process", QStringLiteral("Running: %1 %2 (workDir: %3, timeout: %4ms)")
                              .arg(program, args.join(QLatin1Char(' ')), workDir)
                              .arg(timeoutMs));
    if (stdinData.isEmpty() == false)
    {
        proc.write(stdinData.toUtf8());
        proc.closeWriteChannel();
    }

    result_t res;
    bool finished = proc.waitForFinished(timeoutMs);
    if (finished == false)
    {
        proc.kill();
        QCAI_WARN("Process", QStringLiteral("Process timed out: %1").arg(program));
        res.success = false;
        res.error_string = QStringLiteral("Process timed out after %1 ms").arg(timeoutMs);
        res.std_err = QString::fromUtf8(proc.readAllStandardError());
        return res;
    }

    res.exit_code = proc.exitCode();
    res.success = (proc.exitStatus() == QProcess::NormalExit) && (res.exit_code == 0);
    res.std_out = QString::fromUtf8(proc.readAllStandardOutput());
    res.std_err = QString::fromUtf8(proc.readAllStandardError());
    res.error_string = proc.errorString();
    QCAI_DEBUG("Process", QStringLiteral("Process finished: %1 exitCode=%2 success=%3")
                              .arg(program)
                              .arg(res.exit_code)
                              .arg(res.success ? QStringLiteral("yes") : QStringLiteral("no")));
    return res;
}

void process_runner_t::run_async(const QString &program, const QStringList &args,
                                 const QString &workDir, int timeoutMs)
{
    auto *proc = new QProcess(this);
    if (workDir.isEmpty() == false)
    {
        proc->setWorkingDirectory(workDir);
    }

    // Stream stdout lines
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        this->handle_ready_read();
        Q_UNUSED(proc)
    });

    // Timeout guard
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, proc, [proc]() { proc->kill(); });
    timer->start(timeoutMs);

    connect(proc, &QProcess::finished, this,
            [this, proc, timer](int exit_code, QProcess::ExitStatus status) {
                timer->stop();
                timer->deleteLater();

                result_t res;
                res.exit_code = exit_code;
                res.success = (status == QProcess::NormalExit) && (exit_code == 0);
                res.std_out = QString::fromUtf8(proc->readAllStandardOutput());
                res.std_err = QString::fromUtf8(proc->readAllStandardError());
                res.error_string = proc->errorString();
                proc->deleteLater();
                emit this->finished(res);
            });

    proc->start(program, args);
}

/**
 * @brief Placeholder for future line-oriented async stdout handling.
 */
void process_runner_t::handle_ready_read()
{
    // Not wired to a specific process here; streaming is handled in runAsync lambda above
}

}  // namespace qcai2
