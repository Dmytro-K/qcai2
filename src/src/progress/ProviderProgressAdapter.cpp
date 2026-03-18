#include "ProviderProgressAdapter.h"

#include <QSet>

namespace qcai2
{

namespace
{

class BaseProgressAdapter : public IProviderProgressAdapter
{
public:
    QList<AgentProgressEvent> adapt(const ProviderRawEvent &event) override
    {
        QList<AgentProgressEvent> events;

        switch (event.kind)
        {
            case ProviderRawEventKind::RequestStarted:
                events.append({AgentProgressEventKind::RequestStarted,
                               AgentProgressOperation::None,
                               event.providerId,
                               event.rawType,
                               {},
                               {},
                               event.message});
                break;

            case ProviderRawEventKind::ReasoningDelta:
                if (m_reasoningActive == false)
                {
                    m_reasoningActive = true;
                    events.append({AgentProgressEventKind::ReasoningStarted,
                                   AgentProgressOperation::None,
                                   event.providerId,
                                   event.rawType,
                                   {},
                                   {},
                                   {}});
                }
                events.append({AgentProgressEventKind::ReasoningUpdated,
                               AgentProgressOperation::None,
                               event.providerId,
                               event.rawType,
                               {},
                               {},
                               event.message});
                break;

            case ProviderRawEventKind::MessageDelta:
                events.append({AgentProgressEventKind::MessageDelta,
                               AgentProgressOperation::None,
                               event.providerId,
                               event.rawType,
                               {},
                               {},
                               event.message});
                break;

            case ProviderRawEventKind::ToolStarted: {
                const AgentProgressOperation operation = classifyToolOperation(event.toolName);
                const QString label = progressLabelForTool(event.toolName, operation);
                if (m_activeTools.contains(event.toolName) == false)
                {
                    m_activeTools.insert(event.toolName);
                    events.append({AgentProgressEventKind::ToolStarted, operation,
                                   event.providerId, event.rawType, event.toolName, label,
                                   event.message});
                }
                break;
            }

            case ProviderRawEventKind::ToolCompleted: {
                const AgentProgressOperation operation = classifyToolOperation(event.toolName);
                const QString label = progressLabelForTool(event.toolName, operation);
                m_activeTools.remove(event.toolName);
                events.append({AgentProgressEventKind::ToolCompleted, operation, event.providerId,
                               event.rawType, event.toolName, label, event.message});
                break;
            }

            case ProviderRawEventKind::ResponseCompleted:
            case ProviderRawEventKind::Idle:
                m_reasoningActive = false;
                m_activeTools.clear();
                events.append({AgentProgressEventKind::Idle,
                               AgentProgressOperation::None,
                               event.providerId,
                               event.rawType,
                               {},
                               {},
                               event.message});
                break;

            case ProviderRawEventKind::Error:
                m_reasoningActive = false;
                m_activeTools.clear();
                events.append({AgentProgressEventKind::Error,
                               AgentProgressOperation::None,
                               event.providerId,
                               event.rawType,
                               {},
                               {},
                               event.message});
                break;
        }

        return events;
    }

private:
    bool m_reasoningActive = false;
    QSet<QString> m_activeTools;
};

class CopilotProgressAdapter final : public BaseProgressAdapter
{
};

class OpenAICompatibleProgressAdapter final : public BaseProgressAdapter
{
};

class LocalHttpProgressAdapter final : public BaseProgressAdapter
{
};

class AnthropicProgressAdapter final : public BaseProgressAdapter
{
};

class GeminiProgressAdapter final : public BaseProgressAdapter
{
};

class XAiProgressAdapter final : public BaseProgressAdapter
{
};

}  // namespace

std::unique_ptr<IProviderProgressAdapter> createProviderProgressAdapter(const QString &providerId)
{
    if (providerId == QStringLiteral("copilot"))
    {
        return std::make_unique<CopilotProgressAdapter>();
    }
    if (providerId == QStringLiteral("openai") || providerId == QStringLiteral("ollama"))
    {
        return std::make_unique<OpenAICompatibleProgressAdapter>();
    }
    if (providerId == QStringLiteral("local"))
    {
        return std::make_unique<LocalHttpProgressAdapter>();
    }
    if (providerId == QStringLiteral("anthropic"))
    {
        return std::make_unique<AnthropicProgressAdapter>();
    }
    if (providerId == QStringLiteral("gemini"))
    {
        return std::make_unique<GeminiProgressAdapter>();
    }
    if (providerId == QStringLiteral("xai") || providerId == QStringLiteral("grok"))
    {
        return std::make_unique<XAiProgressAdapter>();
    }

    return std::make_unique<OpenAICompatibleProgressAdapter>();
}

}  // namespace qcai2
