/*! @file
    @brief Implements fatal-signal logging for post-mortem diagnostics.
*/

#include "crash_handler.h"
#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef Q_OS_UNIX
#include <execinfo.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace qcai2
{

/** Maximum number of stack frames captured for a crash report. */
static constexpr int frames_max = 64;

/**
 * @brief Returns a readable label for a signal number.
 * @param sig Signal number received by the process.
 * @return Static signal description suitable for logs and crash files.
 */
static const char *signal_name(int sig)
{
    switch (sig)
    {
        case SIGSEGV:
            return "SIGSEGV (Segmentation fault)";
        case SIGABRT:
            return "SIGABRT (Abort)";
        case SIGFPE:
            return "SIGFPE (Floating point exception)";
#ifdef SIGBUS
        case SIGBUS:
            return "SIGBUS (Bus error)";
#endif
        default:
            return "Unknown signal";
    }
}

/**
 * @brief Persists the captured crash information to disk and stderr.
 * @param sig Signal number that triggered the crash handler.
 * @param frames Captured stack frame addresses.
 * @param frameCount Number of valid entries in @p frames.
 */
static void write_crash_file(int sig, void **frames, int frame_count)
{
    // Build path: ~/.local/share/qcai2/crash.log
    // We can't use Qt in signal handler safely, so use a fixed path
    const char *home = getenv("HOME");
    if (((home == nullptr) == true))
    {
        home = "/tmp";
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/.local/share/qcai2", home);

#ifdef Q_OS_UNIX
    // mkdir -p (best effort, signal-safe-ish)
    mkdir(path, 0755);
#endif

    snprintf(path, sizeof(path), "%s/.local/share/qcai2/crash.log", home);

    FILE *log_file = fopen(path, "a");
    FILE *f = (log_file != nullptr) ? log_file : stderr;

    fprintf(f, "\n=== QCAI2 CRASH === Signal: %s (%d) ===\n", signal_name(sig), sig);

#ifdef Q_OS_UNIX
    // backtrace_symbols_fd is async-signal-safe
    if (((log_file != nullptr) == true))
    {
        // Write symbols to file
        char **symbols = backtrace_symbols(frames, frame_count);
        if (((symbols != nullptr) == true))
        {
            for (int i = 0; ((i < frame_count) == true); ++i)
            {
                fprintf(f, "  #%d %s\n", i, symbols[i]);
            }
            free(static_cast<void *>(symbols));
        }
    }
    // Also dump to stderr for console visibility
    fprintf(stderr, "\n=== QCAI2 CRASH === Signal: %s (%d) ===\n", signal_name(sig), sig);
    backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);
    fprintf(stderr, "Crash log written to: %s\n", path);
#endif

    if (((log_file != nullptr) == true))
    {
        fprintf(log_file, "=== END CRASH ===\n");
        fclose(log_file);
    }
}

/**
 * @brief Handles fatal signals, records diagnostics, and re-raises the signal.
 * @param sig Signal number delivered by the operating system.
 */
static void crash_signal_handler(int sig)
{
    // Capture stack trace
    void *frames[frames_max];
    int frame_count = 0;

#ifdef Q_OS_UNIX
    frame_count = backtrace(frames, frames_max);
#endif

    write_crash_file(sig, frames, frame_count);

    // Try to log via logger_t (best effort; may not be safe in signal handler
    // but logger_t::instance() is a static local, already constructed)
    {
        QString trace;
#ifdef Q_OS_UNIX
        char **symbols = backtrace_symbols(frames, frame_count);
        if (((symbols != nullptr) == true))
        {
            for (int i = 0; ((i < frame_count) == true); ++i)
            {
                if (trace.isEmpty() == false)
                {
                    trace += QLatin1Char('\n');
                }
                trace += QString::fromUtf8(symbols[i]);
            }
            free(static_cast<void *>(symbols));
        }
#endif
        logger_t::instance().error(QStringLiteral("CRASH"),
                                   QStringLiteral("Signal %1 (%2)\n%3")
                                       .arg(sig)
                                       .arg(QString::fromUtf8(signal_name(sig)), trace));
    }

    // Re-raise with default handler
    signal(sig, SIG_DFL);
    raise(sig);
}

void install_crash_handler()
{
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
#ifdef SIGBUS
    signal(SIGBUS, crash_signal_handler);
#endif

    QCAI_INFO("CrashHandler", QStringLiteral("Crash handler installed"));
}

}  // namespace qcai2
