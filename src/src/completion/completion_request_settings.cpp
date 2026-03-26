/*! @file
    @brief Implements helpers that resolve effective settings for one completion request.
*/

#include "completion_request_settings.h"
#include "../../qcai2tr.h"

namespace qcai2
{

namespace
{

QString default_model_for_provider(const settings_t &settings, const QString &provider_id)
{
    if (provider_id == QStringLiteral("copilot"))
    {
        return settings.copilot_model.trimmed().isEmpty() == false
                   ? settings.copilot_model.trimmed()
                   : settings.model_name.trimmed();
    }
    if (provider_id == QStringLiteral("ollama"))
    {
        return settings.ollama_model.trimmed().isEmpty() == false ? settings.ollama_model.trimmed()
                                                                  : settings.model_name.trimmed();
    }
    return settings.model_name.trimmed();
}

bool is_enabled_effort(const QString &value)
{
    const QString normalized = value.trimmed();
    return normalized.isEmpty() == false && normalized != QStringLiteral("off") &&
           normalized != QStringLiteral("none");
}

}  // namespace

completion_request_settings_t resolve_completion_request_settings(const settings_t &settings)
{
    completion_request_settings_t resolved;
    resolved.provider_id = settings.completion_provider.trimmed();
    if (resolved.provider_id.isEmpty() == true)
    {
        resolved.provider_id = settings.provider.trimmed();
    }
    if (resolved.provider_id.isEmpty() == true)
    {
        resolved.provider_id = settings_t().provider;
    }

    resolved.model = settings.completion_model.trimmed();
    if (resolved.model.isEmpty() == true)
    {
        resolved.model = default_model_for_provider(settings, resolved.provider_id);
    }

    resolved.selected_reasoning_effort = settings.completion_reasoning_effort.trimmed();
    if (resolved.selected_reasoning_effort.isEmpty() == true)
    {
        resolved.selected_reasoning_effort = settings_t().completion_reasoning_effort;
    }

    resolved.selected_thinking_level = settings.completion_thinking_level.trimmed();
    if (resolved.selected_thinking_level.isEmpty() == true)
    {
        resolved.selected_thinking_level = settings_t().completion_thinking_level;
    }

    resolved.send_reasoning = settings.completion_send_reasoning;
    resolved.send_thinking = settings.completion_send_thinking;
    return resolved;
}

QString completion_reasoning_effort_to_send(const completion_request_settings_t &settings)
{
    if (settings.send_reasoning == false)
    {
        return {};
    }
    return settings.selected_reasoning_effort.trimmed();
}

QString completion_thinking_instruction(const completion_request_settings_t &settings)
{
    if (settings.send_thinking == false ||
        is_enabled_effort(settings.selected_thinking_level) == false)
    {
        return {};
    }

    return tr_t::tr("Use %1 thinking depth for this task.")
        .arg(settings.selected_thinking_level.trimmed());
}

}  // namespace qcai2
