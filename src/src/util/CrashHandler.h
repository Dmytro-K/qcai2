#pragma once

/*! @file
    @brief Installs process-wide crash reporting hooks for fatal signals.
*/

namespace qcai2
{

/**
 * @brief Installs handlers for fatal signals used by the plugin.
 *
 * The handler records a stack trace, writes a crash log, and re-raises the
 * original signal so the default platform behavior still occurs.
 */
void install_crash_handler();

}  // namespace qcai2
