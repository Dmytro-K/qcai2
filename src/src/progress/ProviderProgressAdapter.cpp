#include "ProviderProgressAdapter.h"

#include <QSet>

namespace qcai2
{

namespace
{

class base_progress_adapter_t : public i_provider_progress_adapter_t
{
public:
    QList<agent_progress_event_t> adapt(const provider_raw_event_t &event) override
    {
        QList<agent_progress_event_t> events;

        switch (event.kind)
        {
            case provider_raw_event_kind_t::REQUEST_STARTED:
                events.append(agent_progress_event_t{agent_progress_event_kind_t::REQUEST_STARTED,
                                                     agent_progress_operation_t::NONE,
                                                     event.provider_id,
                                                     event.raw_type,
                                                     {},
                                                     {},
                                                     event.message});
                break;

            case provider_raw_event_kind_t::REASONING_DELTA:
                if (this->reasoning_active == false)
                {
                    this->reasoning_active = true;
                    events.append(
                        agent_progress_event_t{agent_progress_event_kind_t::REASONING_STARTED,
                                               agent_progress_operation_t::NONE,
                                               event.provider_id,
                                               event.raw_type,
                                               {},
                                               {},
                                               {}});
                }
                events.append(
                    agent_progress_event_t{agent_progress_event_kind_t::REASONING_UPDATED,
                                           agent_progress_operation_t::NONE,
                                           event.provider_id,
                                           event.raw_type,
                                           {},
                                           {},
                                           event.message});
                break;

            case provider_raw_event_kind_t::MESSAGE_DELTA:
                events.append(agent_progress_event_t{agent_progress_event_kind_t::MESSAGE_DELTA,
                                                     agent_progress_operation_t::NONE,
                                                     event.provider_id,
                                                     event.raw_type,
                                                     {},
                                                     {},
                                                     event.message});
                break;

            case provider_raw_event_kind_t::TOOL_STARTED: {
                const agent_progress_operation_t operation =
                    classify_tool_operation(event.tool_name);
                const QString label = progress_label_for_tool(event.tool_name, operation);
                if (this->active_tools.contains(event.tool_name) == false)
                {
                    this->active_tools.insert(event.tool_name);
                    events.append(agent_progress_event_t{
                        agent_progress_event_kind_t::TOOL_STARTED, operation, event.provider_id,
                        event.raw_type, event.tool_name, label, event.message});
                }
                break;
            }

            case provider_raw_event_kind_t::TOOL_COMPLETED: {
                const agent_progress_operation_t operation =
                    classify_tool_operation(event.tool_name);
                const QString label = progress_label_for_tool(event.tool_name, operation);
                this->active_tools.remove(event.tool_name);
                events.append(agent_progress_event_t{agent_progress_event_kind_t::TOOL_COMPLETED,
                                                     operation, event.provider_id, event.raw_type,
                                                     event.tool_name, label, event.message});
                break;
            }

            case provider_raw_event_kind_t::RESPONSE_COMPLETED:
            case provider_raw_event_kind_t::IDLE:
                this->reasoning_active = false;
                this->active_tools.clear();
                events.append(agent_progress_event_t{agent_progress_event_kind_t::IDLE,
                                                     agent_progress_operation_t::NONE,
                                                     event.provider_id,
                                                     event.raw_type,
                                                     {},
                                                     {},
                                                     event.message});
                break;

            case provider_raw_event_kind_t::ERROR_EVENT:
                this->reasoning_active = false;
                this->active_tools.clear();
                events.append(agent_progress_event_t{agent_progress_event_kind_t::ERROR,
                                                     agent_progress_operation_t::NONE,
                                                     event.provider_id,
                                                     event.raw_type,
                                                     {},
                                                     {},
                                                     event.message});
                break;
        }

        return events;
    }

private:
    bool reasoning_active = false;
    QSet<QString> active_tools;
};

class copilot_progress_adapter_t final : public base_progress_adapter_t
{
};

class open_ai_compatible_progress_adapter_t final : public base_progress_adapter_t
{
};

class local_http_progress_adapter_t final : public base_progress_adapter_t
{
};

class anthropic_progress_adapter_t final : public base_progress_adapter_t
{
};

class gemini_progress_adapter_t final : public base_progress_adapter_t
{
};

class x_ai_progress_adapter_t final : public base_progress_adapter_t
{
};

}  // namespace

std::unique_ptr<i_provider_progress_adapter_t>
create_provider_progress_adapter(const QString &provider_id)
{
    if (provider_id == QStringLiteral("copilot"))
    {
        return std::make_unique<copilot_progress_adapter_t>();
    }
    if (provider_id == QStringLiteral("openai") || provider_id == QStringLiteral("ollama"))
    {
        return std::make_unique<open_ai_compatible_progress_adapter_t>();
    }
    if (provider_id == QStringLiteral("local"))
    {
        return std::make_unique<local_http_progress_adapter_t>();
    }
    if (provider_id == QStringLiteral("anthropic"))
    {
        return std::make_unique<anthropic_progress_adapter_t>();
    }
    if (provider_id == QStringLiteral("gemini"))
    {
        return std::make_unique<gemini_progress_adapter_t>();
    }
    if (provider_id == QStringLiteral("xai") || provider_id == QStringLiteral("grok"))
    {
        return std::make_unique<x_ai_progress_adapter_t>();
    }

    return std::make_unique<open_ai_compatible_progress_adapter_t>();
}

}  // namespace qcai2
