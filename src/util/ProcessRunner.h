#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <functional>

namespace Qcai2 {

// Synchronous QProcess wrapper with timeout and allowlist enforcement.
// All destructive commands must pass through this wrapper.
class ProcessRunner : public QObject
{
    Q_OBJECT
public:
    explicit ProcessRunner(QObject *parent = nullptr);

    struct Result {
        bool    success  = false;
        int     exitCode = -1;
        QString stdOut;
        QString stdErr;
        QString errorString; // QProcess error string (e.g. "No such file or directory")
    };

    // Run a command synchronously. workDir defaults to QDir::currentPath().
    // timeoutMs defaults to 30 000 ms.
    Result run(const QString &program,
               const QStringList &args,
               const QString &workDir     = {},
               int            timeoutMs   = 30000,
               const QString &stdinData   = {});

    // Same but asynchronous: emits finished() when done.
    void runAsync(const QString &program,
                  const QStringList &args,
                  const QString &workDir = {},
                  int            timeoutMs = 30000);

signals:
    void finished(const ProcessRunner::Result &result);
    void outputLine(const QString &line); // progressive stdout lines

private:
    void handleReadyRead();
    QString m_pendingLine;
};

} // namespace Qcai2
