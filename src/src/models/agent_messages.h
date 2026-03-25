/*! Declares lightweight chat and response models exchanged with AI providers. */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

namespace qcai2
{

/**
 * One local file attachment referenced from a chat request.
 */
struct file_attachment_t
{
    /** Stable identifier used by the dock UI while editing one request. */
    QString attachment_id;

    /** User-visible source file name shown in previews and logs. */
    QString file_name;

    /** Stored local file path for this attachment. */
    QString storage_path;

    /** MIME type used when serializing the attachment to providers. */
    QString mime_type;

    /** Returns true when the attachment has enough metadata to be used. */
    bool is_valid() const;

    /** Serializes the attachment to JSON. */
    QJsonObject to_json() const;

    /** Restores one attachment from JSON. */
    static file_attachment_t from_json(const QJsonObject &obj);
};

/** Backward-compatible alias kept for image-specific UI code. */
using image_attachment_t = file_attachment_t;

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
     * Optional file attachments supplied alongside the text content.
     */
    QList<file_attachment_t> attachments = {};

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
 * One predefined answer option offered in a structured user-decision request.
 */
struct agent_decision_option_t
{
    /** Stable option identifier returned when the user picks this choice. */
    QString id;

    /** Short user-visible label for the option. */
    QString label;

    /** Optional longer explanation shown with the option. */
    QString description;

    /**
     * Serializes the option to JSON.
     * @return JSON object for provider/debug usage.
     */
    QJsonObject to_json() const;

    /**
     * Deserializes one option from JSON.
     * @param obj JSON object to convert.
     * @return Parsed option payload.
     */
    static agent_decision_option_t from_json(const QJsonObject &obj);
};

/**
 * Structured request asking the user to choose between a small set of options.
 */
struct agent_decision_request_t
{
    /** Stable request identifier used to match the later user answer. */
    QString request_id;

    /** Short user-visible title shown on the decision card. */
    QString title;

    /** Optional longer explanation shown above the options. */
    QString description;

    /** Predefined options the user can pick from. */
    QList<agent_decision_option_t> options;

    /** True when the UI should offer a custom freeform answer field. */
    bool allow_freeform = false;

    /** Placeholder text shown in the custom-answer field when enabled. */
    QString freeform_placeholder;

    /** Option id that should be preselected when the card opens. */
    QString recommended_option_id;

    /**
     * Serializes the request to JSON.
     * @return JSON object for provider/debug usage.
     */
    QJsonObject to_json() const;

    /**
     * Deserializes one decision request from JSON.
     * @param obj JSON object to convert.
     * @return Parsed request payload.
     */
    static agent_decision_request_t from_json(const QJsonObject &obj);

    /**
     * Returns the zero-based index of one option by id.
     * @param option_id Option identifier to find.
     * @return Matching option index, or -1 when no option matches.
     */
    int option_index(const QString &option_id) const;

    /**
     * Reports whether the request contains at least one actionable answer path.
     * @return True when it has options or allows a freeform answer.
     */
    bool is_valid() const;
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
    /** A structured request for a user decision with multiple options. */
    DECISION_REQUEST,
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

    /** Structured decision payload for response_type_t::DECISION_REQUEST. */
    agent_decision_request_t decision_request;

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

/**
 * Extracts the user-facing markdown preview from a possibly partial streamed model response.
 * For structured JSON responses this returns the decoded `final.summary` content as it streams.
 * For plain-text responses it returns the raw text.
 */
QString streaming_response_markdown_preview(const QString &raw);

}  // namespace qcai2

Q_DECLARE_METATYPE(qcai2::agent_decision_option_t)
Q_DECLARE_METATYPE(qcai2::agent_decision_request_t)
