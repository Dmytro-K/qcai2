#pragma once

#include <QString>
#include <QJsonObject>

namespace Qcai2 {

// Represents a single tool invocation request from the LLM
struct ToolCall {
    QString    id;   // optional call id (OpenAI function-calling style)
    QString    name; // tool name, e.g. "read_file"
    QJsonObject args; // tool arguments

    QString    result;   // filled in after execution
    bool       executed = false;
    bool       failed   = false;
    QString    errorMsg;

    QJsonObject toJson() const;
    static ToolCall fromJson(const QJsonObject &obj);
};

} // namespace Qcai2
