/*! Declares the lightweight model used to store one agent tool invocation. */
#pragma once

#include <QJsonObject>
#include <QString>

namespace qcai2
{

/**
 * Represents one tool invocation request and its execution result.
 */
struct ToolCall
{
    /**
     * Optional provider call id used to correlate responses.
     */
    QString id;

    /**
     * Tool name such as "read_file".
     */
    QString name;

    /**
     * JSON argument object passed to the tool.
     */
    QJsonObject args;

    /**
     * Tool result text populated after execution.
     */
    QString result;

    /**
     * True once the tool was executed.
     */
    bool executed = false;

    /**
     * True when execution failed and errorMsg is populated.
     */
    bool failed = false;

    /**
     * Error message for failed executions.
     */
    QString errorMsg;

    /**
     * Serializes the request and optional execution result to JSON.
     */
    QJsonObject toJson() const;

    /**
     * Deserializes the tool request fields from JSON.
     * @param obj JSON object to convert.
     */
    static ToolCall fromJson(const QJsonObject &obj);

};

}  // namespace qcai2
