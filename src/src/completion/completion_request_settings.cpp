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

completion_engine_kind_t parse_completion_engine_kind(const QString &value)
{
    return value.trimmed() == QStringLiteral("qompi") ? completion_engine_kind_t::QOMPI
                                                      : completion_engine_kind_t::LEGACY;
}

}  // namespace

completion_engine_kind_t resolve_completion_engine_kind(const settings_t &settings)
{
    return parse_completion_engine_kind(settings.completion_engine);
}

completion_request_settings_t resolve_completion_request_settings(const settings_t &settings)
{
    completion_request_settings_t resolved;
    resolved.engine_kind = resolve_completion_engine_kind(settings);
    const bool use_qompi_settings = resolved.engine_kind == completion_engine_kind_t::QOMPI;

    resolved.provider_id = use_qompi_settings ? settings.qompi_completion_provider.trimmed()
                                              : settings.completion_provider.trimmed();
    if (resolved.provider_id.isEmpty() == true)
    {
        resolved.provider_id = settings.provider.trimmed();
    }
    if (resolved.provider_id.isEmpty() == true)
    {
        resolved.provider_id = settings_t().provider;
    }

    resolved.model = use_qompi_settings ? settings.qompi_completion_model.trimmed()
                                        : settings.completion_model.trimmed();
    if (resolved.model.isEmpty() == true)
    {
        resolved.model = default_model_for_provider(settings, resolved.provider_id);
    }

    resolved.selected_reasoning_effort = use_qompi_settings
                                             ? settings.qompi_completion_reasoning_effort.trimmed()
                                             : settings.completion_reasoning_effort.trimmed();
    if (resolved.selected_reasoning_effort.isEmpty() == true)
    {
        resolved.selected_reasoning_effort = use_qompi_settings
                                                 ? settings_t().qompi_completion_reasoning_effort
                                                 : settings_t().completion_reasoning_effort;
    }

    resolved.selected_thinking_level = use_qompi_settings
                                           ? settings.qompi_completion_thinking_level.trimmed()
                                           : settings.completion_thinking_level.trimmed();
    if (resolved.selected_thinking_level.isEmpty() == true)
    {
        resolved.selected_thinking_level = use_qompi_settings
                                               ? settings_t().qompi_completion_thinking_level
                                               : settings_t().completion_thinking_level;
    }

    resolved.send_reasoning = use_qompi_settings ? settings.qompi_completion_send_reasoning
                                                 : settings.completion_send_reasoning;
    resolved.send_thinking = use_qompi_settings ? settings.qompi_completion_send_thinking
                                                : settings.completion_send_thinking;
    return resolved;
}

completion_connection_settings_t resolve_completion_connection_settings(const settings_t &settings)
{
    const completion_request_settings_t request_settings =
        resolve_completion_request_settings(settings);

    completion_connection_settings_t resolved;
    resolved.provider_id = request_settings.provider_id;
    resolved.base_url = settings.completion_base_url.trimmed().isEmpty() == false
                            ? settings.completion_base_url.trimmed()
                            : settings.base_url.trimmed();
    resolved.api_key = settings.completion_api_key.isEmpty() == false ? settings.completion_api_key
                                                                      : settings.api_key;
    resolved.local_base_url = settings.completion_local_base_url.trimmed().isEmpty() == false
                                  ? settings.completion_local_base_url.trimmed()
                                  : settings.local_base_url.trimmed();
    resolved.local_endpoint_path =
        settings.completion_local_endpoint_path.trimmed().isEmpty() == false
            ? settings.completion_local_endpoint_path.trimmed()
            : settings.local_endpoint_path.trimmed();
    resolved.local_custom_headers =
        settings.completion_local_custom_headers.trimmed().isEmpty() == false
            ? settings.completion_local_custom_headers.trimmed()
            : settings.local_custom_headers.trimmed();
    resolved.ollama_base_url = settings.completion_ollama_base_url.trimmed().isEmpty() == false
                                   ? settings.completion_ollama_base_url.trimmed()
                                   : settings.ollama_base_url.trimmed();
    resolved.copilot_node_path = settings.completion_copilot_node_path.trimmed().isEmpty() == false
                                     ? settings.completion_copilot_node_path.trimmed()
                                     : settings.copilot_node_path.trimmed();
    resolved.copilot_sidecar_path =
        settings.completion_copilot_sidecar_path.trimmed().isEmpty() == false
            ? settings.completion_copilot_sidecar_path.trimmed()
            : settings.copilot_sidecar_path.trimmed();
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
