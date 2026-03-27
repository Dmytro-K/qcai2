/*! @file
    @brief Declares helpers that apply completion-specific provider connection overrides.
*/

#pragma once

#include "completion_request_settings.h"

namespace qcai2
{

class iai_provider_t;

/**
 * Applies effective completion connection settings to one provider instance.
 * @param provider Non-owning provider instance used for completion requests.
 * @param settings Effective completion connection settings.
 */
void apply_completion_connection_settings(iai_provider_t *provider,
                                          const completion_connection_settings_t &settings);

/**
 * Builds one redacted human-readable summary of effective completion connection settings.
 * @param settings Effective completion connection settings.
 * @return Provider-specific diagnostics string without secrets.
 */
QString describe_completion_connection_settings(const completion_connection_settings_t &settings);

}  // namespace qcai2
