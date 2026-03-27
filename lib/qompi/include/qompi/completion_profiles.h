/*! @file
    @brief Declares provider/model profile metadata and runtime config resolution helpers.
*/

#pragma once

#include "completion_policy.h"
#include "completion_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qompi
{

/**
 * Describes backend/model features relevant to completion planning.
 */
struct completion_capabilities_t
{
    bool supports_streaming = true;
    bool supports_cancellation = false;
    bool supports_reasoning_effort = false;
    bool supports_thinking_level = false;
    bool supports_fill_in_the_middle = false;
    bool supports_temperature = true;
    bool supports_seed = false;
    bool supports_stop_words = true;
};

/**
 * Stores runtime limits used for request planning.
 */
struct completion_runtime_limits_t
{
    std::size_t max_context_tokens = 0;
    std::int32_t default_max_output_tokens = 128;
    std::int32_t max_output_tokens_limit = 0;
    std::size_t max_stop_words = 4;
};

/**
 * Stores one model entry exposed by a provider profile.
 */
struct model_profile_t
{
    std::string model_id;
    std::string display_name;
    double default_temperature = 0.0;
    completion_capabilities_t capabilities;
    completion_runtime_limits_t limits;
};

/**
 * Stores one provider catalog entry used to resolve effective completion config.
 */
struct provider_profile_t
{
    std::string provider_id;
    std::string display_name;
    std::string default_model_id;
    double default_temperature = 0.0;
    completion_capabilities_t default_capabilities;
    completion_runtime_limits_t default_limits;
    std::vector<std::string> provider_stop_words;
    std::vector<model_profile_t> models;
};

/**
 * Stores the effective runtime config chosen for one request.
 */
struct resolved_completion_config_t
{
    std::string provider_id;
    std::string provider_display_name;
    std::string model_id;
    completion_capabilities_t capabilities;
    completion_runtime_limits_t limits;
    completion_stop_policy_t stop_policy;
    completion_options_t options;
};

/**
 * Finds one model entry inside a provider profile.
 * @param provider_profile Provider profile to inspect.
 * @param model_id Model identifier to match.
 * @return Matching model profile or nullptr when not found.
 */
const model_profile_t *find_model_profile(const provider_profile_t &provider_profile,
                                          std::string_view model_id);

/**
 * Resolves model defaults, stop-policy planning, and capability-driven option filtering.
 * @param provider_profile Provider profile that supplies model metadata.
 * @param request Completion request that provides cursor context for stop planning.
 * @param options Request options before capability filtering.
 * @return Effective runtime config used for dispatch.
 */
resolved_completion_config_t resolve_completion_config(const provider_profile_t &provider_profile,
                                                       const completion_request_t &request,
                                                       completion_options_t options);

}  // namespace qompi
