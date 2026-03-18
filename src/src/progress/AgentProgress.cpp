#include "AgentProgress.h"

namespace qcai2
{

namespace
{

QString normalizedToolName(QString toolName)
{
    toolName = toolName.trimmed().toLower();
    toolName.replace(QLatin1Char('-'), QLatin1Char('_'));
    toolName.replace(QLatin1Char(' '), QLatin1Char('_'));
    return toolName;
}

}  // namespace

QString providerRawEventKindName(ProviderRawEventKind kind)
{
    switch (kind)
    {
        case ProviderRawEventKind::RequestStarted:
            return QStringLiteral("request_started");
        case ProviderRawEventKind::ReasoningDelta:
            return QStringLiteral("reasoning_delta");
        case ProviderRawEventKind::MessageDelta:
            return QStringLiteral("message_delta");
        case ProviderRawEventKind::ToolStarted:
            return QStringLiteral("tool_started");
        case ProviderRawEventKind::ToolCompleted:
            return QStringLiteral("tool_completed");
        case ProviderRawEventKind::ResponseCompleted:
            return QStringLiteral("response_completed");
        case ProviderRawEventKind::Idle:
            return QStringLiteral("idle");
        case ProviderRawEventKind::Error:
            return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

QString agentProgressEventKindName(AgentProgressEventKind kind)
{
    switch (kind)
    {
        case AgentProgressEventKind::RequestStarted:
            return QStringLiteral("request_started");
        case AgentProgressEventKind::ReasoningStarted:
            return QStringLiteral("reasoning_started");
        case AgentProgressEventKind::ReasoningUpdated:
            return QStringLiteral("reasoning_updated");
        case AgentProgressEventKind::ToolStarted:
            return QStringLiteral("tool_started");
        case AgentProgressEventKind::ToolCompleted:
            return QStringLiteral("tool_completed");
        case AgentProgressEventKind::ValidationStarted:
            return QStringLiteral("validation_started");
        case AgentProgressEventKind::ValidationCompleted:
            return QStringLiteral("validation_completed");
        case AgentProgressEventKind::MessageDelta:
            return QStringLiteral("message_delta");
        case AgentProgressEventKind::FinalAnswerStarted:
            return QStringLiteral("final_answer_started");
        case AgentProgressEventKind::FinalAnswerCompleted:
            return QStringLiteral("final_answer_completed");
        case AgentProgressEventKind::Idle:
            return QStringLiteral("idle");
        case AgentProgressEventKind::Error:
            return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

QString agentProgressOperationName(AgentProgressOperation operation)
{
    switch (operation)
    {
        case AgentProgressOperation::None:
            return QStringLiteral("none");
        case AgentProgressOperation::Explore:
            return QStringLiteral("explore");
        case AgentProgressOperation::Search:
            return QStringLiteral("search");
        case AgentProgressOperation::Read:
            return QStringLiteral("read");
        case AgentProgressOperation::Test:
            return QStringLiteral("test");
        case AgentProgressOperation::Build:
            return QStringLiteral("build");
        case AgentProgressOperation::ApplyChanges:
            return QStringLiteral("apply_changes");
        case AgentProgressOperation::Generic:
            return QStringLiteral("generic");
    }

    return QStringLiteral("unknown");
}

QString prettifyToolName(const QString &toolName)
{
    QString pretty = normalizedToolName(toolName);
    pretty.replace(QLatin1Char('_'), QLatin1Char(' '));
    return pretty.trimmed();
}

AgentProgressOperation classifyToolOperation(const QString &toolName)
{
    const QString normalized = normalizedToolName(toolName);

    if (normalized.contains(QStringLiteral("compact")) == true)
    {
        return AgentProgressOperation::Explore;
    }
    if (normalized.contains(QStringLiteral("search")) == true ||
        normalized.contains(QStringLiteral("grep")) == true ||
        normalized.contains(QStringLiteral("find")) == true)
    {
        return AgentProgressOperation::Search;
    }
    if (normalized.contains(QStringLiteral("read")) == true)
    {
        return AgentProgressOperation::Read;
    }
    if (normalized.contains(QStringLiteral("test")) == true)
    {
        return AgentProgressOperation::Test;
    }
    if (normalized.contains(QStringLiteral("build")) == true)
    {
        return AgentProgressOperation::Build;
    }
    if (normalized.contains(QStringLiteral("apply_patch")) == true ||
        normalized.contains(QStringLiteral("edit")) == true ||
        normalized.contains(QStringLiteral("write_file")) == true ||
        normalized.contains(QStringLiteral("create_file")) == true)
    {
        return AgentProgressOperation::ApplyChanges;
    }

    return AgentProgressOperation::Generic;
}

QString progressLabelForTool(const QString &toolName, AgentProgressOperation operation)
{
    const QString normalized = normalizedToolName(toolName);

    if (normalized.contains(QStringLiteral("compact")) == true)
    {
        return QStringLiteral("compact command");
    }
    if (operation == AgentProgressOperation::Search && normalized == QStringLiteral("search_repo"))
    {
        return QStringLiteral("repository");
    }
    if (operation == AgentProgressOperation::Read && normalized == QStringLiteral("read_file"))
    {
        return QStringLiteral("file");
    }
    if (operation == AgentProgressOperation::Build || operation == AgentProgressOperation::Test ||
        operation == AgentProgressOperation::ApplyChanges)
    {
        return {};
    }

    return prettifyToolName(toolName);
}

}  // namespace qcai2
