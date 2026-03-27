/*! @file
    @brief Implements provider/model profile resolution helpers.
*/

#include "qompi/completion_profiles.h"

#include <algorithm>
#include <utility>

namespace qompi
{

const model_profile_t *find_model_profile(const provider_profile_t &provider_profile,
                                          std::string_view model_id)
{
    for (const model_profile_t &model_profile : provider_profile.models)
    {
        if (model_profile.model_id == model_id)
        {
            return &model_profile;
        }
    }
    return nullptr;
}

resolved_completion_config_t resolve_completion_config(const provider_profile_t &provider_profile,
                                                       const completion_request_t &request,
                                                       completion_options_t options)
{
    resolved_completion_config_t resolved_config;
    resolved_config.provider_id = provider_profile.provider_id;
    resolved_config.provider_display_name = provider_profile.display_name;
    resolved_config.capabilities = provider_profile.default_capabilities;
    resolved_config.limits = provider_profile.default_limits;

    std::string model_id = options.model;
    if (model_id.empty() == true)
    {
        model_id = provider_profile.default_model_id;
    }
    if (model_id.empty() == true && provider_profile.models.empty() == false)
    {
        model_id = provider_profile.models.front().model_id;
    }

    const model_profile_t *model_profile = nullptr;
    if (model_id.empty() == false)
    {
        model_profile = find_model_profile(provider_profile, model_id);
    }
    if (model_profile == nullptr && provider_profile.models.empty() == false)
    {
        model_profile = &provider_profile.models.front();
        model_id = model_profile->model_id;
    }

    if (model_profile != nullptr)
    {
        resolved_config.capabilities = model_profile->capabilities;
        resolved_config.limits = model_profile->limits;
        if (options.temperature.has_value() == false)
        {
            options.temperature = model_profile->default_temperature;
        }
    }
    else if (options.temperature.has_value() == false)
    {
        options.temperature = provider_profile.default_temperature;
    }

    if (options.max_output_tokens.has_value() == false)
    {
        options.max_output_tokens = resolved_config.limits.default_max_output_tokens;
    }
    if (resolved_config.limits.max_output_tokens_limit > 0 &&
        options.max_output_tokens.has_value() == true)
    {
        options.max_output_tokens = std::min(options.max_output_tokens.value(),
                                             resolved_config.limits.max_output_tokens_limit);
    }

    if (resolved_config.capabilities.supports_reasoning_effort == false)
    {
        options.reasoning_effort.clear();
    }
    if (resolved_config.capabilities.supports_thinking_level == false)
    {
        options.thinking_level.clear();
    }
    if (resolved_config.capabilities.supports_temperature == false)
    {
        options.temperature.reset();
    }
    if (resolved_config.capabilities.supports_seed == false)
    {
        options.seed.reset();
    }

    std::vector<std::string> combined_stop_words = provider_profile.provider_stop_words;
    combined_stop_words.insert(combined_stop_words.end(), options.stop_words.begin(),
                               options.stop_words.end());
    options.stop_words = std::move(combined_stop_words);
    if (resolved_config.capabilities.supports_stop_words == false)
    {
        options.stop_words.clear();
    }
    else if (resolved_config.limits.max_stop_words > 0 &&
             options.stop_words.size() > resolved_config.limits.max_stop_words)
    {
        options.stop_words.resize(resolved_config.limits.max_stop_words);
    }

    resolved_config.model_id = model_id;
    resolved_config.stop_policy = build_completion_stop_policy(request, options);
    if (resolved_config.capabilities.supports_stop_words == false)
    {
        resolved_config.stop_policy.stop_words.clear();
    }
    else if (resolved_config.limits.max_stop_words > 0 &&
             resolved_config.stop_policy.stop_words.size() > resolved_config.limits.max_stop_words)
    {
        resolved_config.stop_policy.stop_words.resize(resolved_config.limits.max_stop_words);
    }
    resolved_config.options = std::move(options);
    resolved_config.options.model = resolved_config.model_id;
    resolved_config.options.stop_words = resolved_config.stop_policy.stop_words;
    return resolved_config;
}

}  // namespace qompi
