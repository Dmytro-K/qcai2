#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>

namespace Qcai2 {

// Single chat message (role + content)
struct ChatMessage {
    QString role;    // "system", "user", "assistant"
    QString content;

    QJsonObject toJson() const;
    static ChatMessage fromJson(const QJsonObject &obj);
};

// A step in the agent plan
struct PlanStep {
    int     index;
    QString description;
    bool    completed = false;
};

// Possible response types from the LLM
enum class ResponseType {
    Plan,         // {"type":"plan", "steps":[...]}
    ToolCall,     // {"type":"tool_call", "name":"...", "args":{...}}
    Final,        // {"type":"final", "summary":"...", "diff":"..."}
    NeedApproval, // {"type":"need_approval", "action":"...", "reason":"...", "preview":"..."}
    Text,         // raw text (fallback)
    Error
};

struct AgentResponse {
    ResponseType type = ResponseType::Error;

    // Plan
    QList<PlanStep> steps;

    // ToolCall
    QString toolName;
    QJsonObject toolArgs;

    // Final
    QString summary;
    QString diff;

    // NeedApproval
    QString approvalAction;
    QString approvalReason;
    QString approvalPreview;

    // Text / Error
    QString text;

    // Parse LLM output (JSON or extract JSON from text)
    static AgentResponse parse(const QString &raw);

private:
    static AgentResponse parseJson(const QJsonObject &obj);
};

} // namespace Qcai2
