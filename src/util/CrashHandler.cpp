/*! @file
    @brief Implements fatal-signal logging for post-mortem diagnostics.
*/

#include "CrashHandler.h"
#include "Logger.h"

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

#ifdef Q_OS_WIN
#include <dbghelp.h>
#include <windows.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace qcai2
{

/** Maximum number of stack frames captured for a crash report. */
static constexpr int kMaxFrames = 64;

/**
 * @brief Returns a readable label for a signal number.
 * @param sig Signal number received by the process.
 * @return Static signal description suitable for logs and crash files.
 */
static const char *signalName(int sig)
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
static void writeCrashFile(int sig, void **frames, int frameCount)
{
    // Build path: ~/.local/share/qcai2/crash.log
    // We can't use Qt in signal handler safely, so use a fixed path
    const char *home = getenv("HOME");
    if (!home)
        home = "/tmp";

    char path[512];
    snprintf(path, sizeof(path), "%s/.local/share/qcai2", home);

#ifdef Q_OS_UNIX
    // mkdir -p (best effort, signal-safe-ish)
    mkdir(path, 0755);
#endif

    snprintf(path, sizeof(path), "%s/.local/share/qcai2/crash.log", home);

    FILE *f = fopen(path, "a");
    if (!f)
        f = stderr;

    fprintf(f, "\n=== QCAI2 CRASH === Signal: %s (%d) ===\n", signalName(sig), sig);

#ifdef Q_OS_UNIX
    // backtrace_symbols_fd is async-signal-safe
    if (f != stderr)
    {
        // Write symbols to file
        char **symbols = backtrace_symbols(frames, frameCount);
        if (symbols)
        {
            for (int i = 0; i < frameCount; ++i)
                fprintf(f, "  #%d %s\n", i, symbols[i]);
            free(symbols);
        }
    }
    // Also dump to stderr for console visibility
    fprintf(stderr, "\n=== QCAI2 CRASH === Signal: %s (%d) ===\n", signalName(sig), sig);
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
    fprintf(stderr, "Crash log written to: %s\n", path);
#endif

    if (f != stderr)
    {
        fprintf(f, "=== END CRASH ===\n");
        fclose(f);
    }
}

/**
 * @brief Handles fatal signals, records diagnostics, and re-raises the signal.
 * @param sig Signal number delivered by the operating system.
 */
static void crashSignalHandler(int sig)
{
    // Capture stack trace
    void *frames[kMaxFrames];
    int frameCount = 0;

#ifdef Q_OS_UNIX
    frameCount = backtrace(frames, kMaxFrames);
#endif

    writeCrashFile(sig, frames, frameCount);

    // Try to log via Logger (best effort; may not be safe in signal handler
    // but Logger::instance() is a static local, already constructed)
    {
        QString trace;
#ifdef Q_OS_UNIX
        char **symbols = backtrace_symbols(frames, frameCount);
        if (symbols)
        {
            for (int i = 0; i < frameCount; ++i)
            {
                if (!trace.isEmpty())
                    trace += QLatin1Char('\n');
                trace += QString::fromUtf8(symbols[i]);
            }
            free(symbols);
        }
#endif
        Logger::instance().error(QStringLiteral("CRASH"),
                                 QStringLiteral("Signal %1 (%2)\n%3")
                                     .arg(sig)
                                     .arg(QString::fromUtf8(signalName(sig)), trace));
    }

    // Re-raise with default handler
    signal(sig, SIG_DFL);
    raise(sig);
}

void installCrashHandler()
{
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
    signal(SIGFPE, crashSignalHandler);
#ifdef SIGBUS
    signal(SIGBUS, crashSignalHandler);
#endif

    QCAI_INFO("CrashHandler", QStringLiteral("Crash handler installed"));
}

}  // namespace qcai2
