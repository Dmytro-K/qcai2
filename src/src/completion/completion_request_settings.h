/*! @file
    @brief Declares helpers that resolve effective settings for one completion request.
*/

#pragma once

#include "../settings/settings.h"

#include <QString>

namespace qcai2
{

/**
 * Stores the effective provider/model and send flags for one completion request.
 */
struct completion_request_settings_t
{
    /** Effective provider identifier used for completion requests. */
    QString provider_id;

    /** Effective model name used for completion requests. */
    QString model;

    /** Configured completion reasoning effort selected in settings. */
    QString selected_reasoning_effort;

    /** Configured completion thinking level selected in settings. */
    QString selected_thinking_level;

    /** Whether completion requests should send reasoning effort. */
    bool send_reasoning = false;

    /** Whether completion requests should send a thinking instruction. */
    bool send_thinking = false;
};

/**
 * Resolves the effective completion request settings from persisted plugin settings.
 * @param settings Current plugin settings snapshot.
 * @return Effective completion request settings.
 */
completion_request_settings_t resolve_completion_request_settings(const settings_t &settings);

/**
 * Returns the reasoning effort value that should actually be sent to the provider.
 * @param settings Effective completion request settings.
 * @return Reasoning effort value, or empty when disabled.
 */
QString completion_reasoning_effort_to_send(const completion_request_settings_t &settings);

/**
 * Returns the optional thinking instruction appended as a system message for completion.
 * @param settings Effective completion request settings.
 * @return Thinking instruction text, or empty when disabled.
 */
QString completion_thinking_instruction(const completion_request_settings_t &settings);

}  // namespace qcai2
