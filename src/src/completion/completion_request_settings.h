/*! @file
    @brief Declares helpers that resolve effective settings for one completion request.
*/

#pragma once

#include "../settings/settings.h"

#include <QString>

namespace qcai2
{

/**
 * Selects which internal completion implementation should execute one request.
 */
enum class completion_engine_kind_t
{
    LEGACY,
    QOMPI,
};

/**
 * Stores the effective provider/model and send flags for one completion request.
 */
struct completion_request_settings_t
{
    /** Effective completion engine selected for this request. */
    completion_engine_kind_t engine_kind = completion_engine_kind_t::LEGACY;

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
 * Stores the effective provider connection settings for one completion request.
 */
struct completion_connection_settings_t
{
    /** Effective provider identifier used for completion requests. */
    QString provider_id;

    /** Effective remote API base URL. */
    QString base_url;

    /** Effective API key for remote/local providers. */
    QString api_key;

    /** Effective Local HTTP base URL. */
    QString local_base_url;

    /** Effective Local HTTP endpoint path. */
    QString local_endpoint_path;

    /** Effective Local HTTP custom headers. */
    QString local_custom_headers;

    /** Effective Ollama base URL. */
    QString ollama_base_url;

    /** Effective Copilot Node executable path. */
    QString copilot_node_path;

    /** Effective Copilot sidecar path. */
    QString copilot_sidecar_path;
};

/**
 * Resolves the effective completion request settings from persisted plugin settings.
 * @param settings Current plugin settings snapshot.
 * @return Effective completion request settings.
 */
completion_request_settings_t resolve_completion_request_settings(const settings_t &settings);

/**
 * Resolves effective provider connection settings for the active completion engine.
 * @param settings Current plugin settings snapshot.
 * @return Effective completion connection settings with completion overrides applied.
 */
completion_connection_settings_t
resolve_completion_connection_settings(const settings_t &settings);

/**
 * Resolves the selected completion engine from persisted plugin settings.
 * @param settings Current plugin settings snapshot.
 * @return Effective completion engine.
 */
completion_engine_kind_t resolve_completion_engine_kind(const settings_t &settings);

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
