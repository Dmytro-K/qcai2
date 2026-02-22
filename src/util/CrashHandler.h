#pragma once

namespace Qcai2 {

// Installs signal handlers (SIGSEGV, SIGABRT, SIGFPE, SIGBUS)
// that capture a stack trace and write it to the plugin debug log
// and to a crash file before re-raising the signal.
void installCrashHandler();

} // namespace Qcai2
