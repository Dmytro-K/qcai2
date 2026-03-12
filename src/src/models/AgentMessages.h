/*! Declares lightweight chat and response models exchanged with AI providers. */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace qcai2
{

/**
 * One chat message passed to or from a provider.
 */
struct ChatMessage
{
    /**
     * Message role such as "system", "user", or "assistant".
     */
    QString role;

    /**
     * Plain text content for the message.
     */
    QString content;

    /**
     * Serializes the message to provider-friendly JSON.
     */
    QJsonObject toJson() const;

    /**
     * Deserializes one chat message from JSON.
     * @param obj JSON object to convert.
     */
    static ChatMessage fromJson(const QJsonObject &obj);

};

/**
 * One step in a multi-step plan returned by the model.
 */
struct PlanStep
{
    /** Zero-based step index in the plan list. */
    int index;

    /** Human-readable step description. */
    QString description;

    /** True when the UI should present the step as completed. */
    bool completed = false;

};

/**
 * High-level response shape parsed from model output.
 */
enum class ResponseType : std::uint8_t
{
    /** A multi-step plan response. */
    Plan,
    /** A request to execute one tool. */
    ToolCall,
    /** A final summary, optionally with a diff. */
    Final,
    /** A request for explicit user approval. */
    NeedApproval,
    /** Unstructured fallback text. */
    Text,
    /** Parsing failure or unsupported structured content. */
    Error
};

/**
 * Parsed provider response normalized into one controller-friendly shape.
 */
struct AgentResponse
{
    /** Parsed response kind. */
    ResponseType type = ResponseType::Error;

    /** Plan steps for ResponseType::Plan. */
    QList<PlanStep> steps;

    /** Tool name for ResponseType::ToolCall. */
    QString toolName;

    /** Tool arguments for ResponseType::ToolCall. */
    QJsonObject toolArgs;

    /** Final summary for ResponseType::Final. */
    QString summary;

    /** Unified diff preview for ResponseType::Final. */
    QString diff;

    /** Action name for ResponseType::NeedApproval. */
    QString approvalAction;

    /** Reason presented to the user for approval. */
    QString approvalReason;

    /** Preview payload shown with the approval request. */
    QString approvalPreview;

    /** Raw text for ResponseType::Text or ResponseType::Error. */
    QString text;

    /**
     * Parses provider output from raw JSON or JSON embedded in text.
     * @param raw Provider response text.
     */
    static AgentResponse parse(const QString &raw);

private:
    /**
     * Parses one JSON object that already represents a response payload.
     * @param obj JSON object to convert.
     */
    static AgentResponse parseJson(const QJsonObject &obj);

};

}  // namespace qcai2
