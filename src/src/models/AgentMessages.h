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
struct chat_message_t
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
    QJsonObject to_json() const;

    /**
     * Deserializes one chat message from JSON.
     * @param obj JSON object to convert.
     */
    static chat_message_t from_json(const QJsonObject &obj);
};

/**
 * One step in a multi-step plan returned by the model.
 */
struct plan_step_t
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
enum class response_type_t : std::uint8_t
{
    /** A multi-step plan response. */
    PLAN,
    /** A request to execute one tool. */
    TOOL_CALL,
    /** A final summary, optionally with a diff. */
    FINAL,
    /** A request for explicit user approval. */
    NEED_APPROVAL,
    /** Unstructured fallback text. */
    TEXT,
    /** Parsing failure or unsupported structured content. */
    ERROR
};

/**
 * Parsed provider response normalized into one controller-friendly shape.
 */
struct agent_response_t
{
    /** Parsed response kind. */
    response_type_t type = response_type_t::ERROR;

    /** Plan steps for response_type_t::PLAN. */
    QList<plan_step_t> steps;

    /** Tool name for response_type_t::TOOL_CALL. */
    QString tool_name;

    /** Tool arguments for response_type_t::TOOL_CALL. */
    QJsonObject tool_args;

    /** Final summary for response_type_t::FINAL. */
    QString summary;

    /** Unified diff preview for response_type_t::FINAL. */
    QString diff;

    /** Action name for response_type_t::NEED_APPROVAL. */
    QString approval_action;

    /** Reason presented to the user for approval. */
    QString approval_reason;

    /** Preview payload shown with the approval request. */
    QString approval_preview;

    /** Raw text for response_type_t::TEXT or response_type_t::ERROR. */
    QString text;

    /**
     * Parses provider output from raw JSON or JSON embedded in text.
     * @param raw Provider response text.
     */
    static agent_response_t parse(const QString &raw);

private:
    /**
     * Parses one JSON object that already represents a response payload.
     * @param obj JSON object to convert.
     */
    static agent_response_t parse_json(const QJsonObject &obj);
};

}  // namespace qcai2
