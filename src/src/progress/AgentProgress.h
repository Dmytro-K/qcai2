/*! Declares normalized provider-agnostic progress events and shared tool
 *  classification helpers for agent status reporting.
 */
#pragma once

#include <QString>

#include <cstdint>

namespace qcai2
{

enum class ProviderRawEventKind : std::uint8_t
{
    RequestStarted,
    ReasoningDelta,
    MessageDelta,
    ToolStarted,
    ToolCompleted,
    ResponseCompleted,
    Idle,
    Error
};

struct ProviderRawEvent
{
    ProviderRawEventKind kind = ProviderRawEventKind::RequestStarted;
    QString providerId;
    QString rawType;
    QString toolName;
    QString message;
};

enum class AgentProgressEventKind : std::uint8_t
{
    RequestStarted,
    ReasoningStarted,
    ReasoningUpdated,
    ToolStarted,
    ToolCompleted,
    ValidationStarted,
    ValidationCompleted,
    MessageDelta,
    FinalAnswerStarted,
    FinalAnswerCompleted,
    Idle,
    Error
};

enum class AgentProgressOperation : std::uint8_t
{
    None,
    Explore,
    Search,
    Read,
    Test,
    Build,
    ApplyChanges,
    Generic
};

struct AgentProgressEvent
{
    AgentProgressEventKind kind = AgentProgressEventKind::Idle;
    AgentProgressOperation operation = AgentProgressOperation::None;
    QString providerId;
    QString rawType;
    QString toolName;
    QString label;
    QString message;
};

QString providerRawEventKindName(ProviderRawEventKind kind);
QString agentProgressEventKindName(AgentProgressEventKind kind);
QString agentProgressOperationName(AgentProgressOperation operation);
QString prettifyToolName(const QString &toolName);
AgentProgressOperation classifyToolOperation(const QString &toolName);
QString progressLabelForTool(const QString &toolName, AgentProgressOperation operation);

}  // namespace qcai2
