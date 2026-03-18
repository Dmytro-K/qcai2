#pragma once

/*! @file
    @brief Thin wrapper around QProcess for bounded command execution.
*/

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

namespace qcai2
{

/**
 * @brief Runs external commands with timeout handling for plugin tools.
 */
class process_runner_t : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Describes the outcome of a completed process.
     */
    struct result_t
    {
        /** @brief True when the process exits normally with code zero. */
        bool success = false;

        /** @brief Exit code reported by the child process. */
        int exit_code = -1;

        /** @brief Captured standard output. */
        QString std_out;

        /** @brief Captured standard error. */
        QString std_err;

        /** @brief QProcess error text such as startup failures. */
        QString error_string;
    };

    /**
     * @brief Creates a process runner owned by an optional parent object.
     * @param parent QObject parent for lifecycle management.
     */
    explicit process_runner_t(QObject *parent = nullptr);

    /**
     * @brief Runs a command synchronously.
     * @param program Executable name or path.
     * @param args Command-line arguments.
     * @param workDir Optional working directory for the child process.
     * @param timeoutMs Maximum wait time before the process is killed.
     * @param stdinData Optional text written to the process stdin.
     * @return Captured process result including stdout, stderr, and exit code.
     */
    result_t run(const QString &program, const QStringList &args, const QString &work_dir = {},
                 int timeout_ms = 30000, const QString &stdin_data = {});

    /**
     * @brief Starts a command asynchronously.
     * @param program Executable name or path.
     * @param args Command-line arguments.
     * @param workDir Optional working directory for the child process.
     * @param timeoutMs Maximum runtime before the child process is killed.
     */
    void run_async(const QString &program, const QStringList &args, const QString &work_dir = {},
                   int timeout_ms = 30000);

signals:
    /**
     * @brief Emitted when an asynchronous process finishes.
     * @param result Captured process outcome.
     */
    void finished(const process_runner_t::result_t &result);

    /**
     * @brief Reserved signal for line-oriented asynchronous output.
     * @param line Parsed stdout line.
     */
    void output_line(const QString &line);

private:
    /**
     * @brief Placeholder hook for parsing asynchronous stdout.
     */
    void handle_ready_read();

    /** @brief Buffered partial line for async stdout parsing. */
    QString pending_line;
};

}  // namespace qcai2
