/*! Implements JSON serialization helpers for one agent tool invocation. */
#include "ToolCall.h"
#include <QJsonDocument>

namespace qcai2
{

QJsonObject ToolCall::toJson() const
{
    QJsonObject o;
    if (!id.isEmpty())
        o["id"] = id;
    o["name"] = name;
    o["args"] = args;
    if (executed)
    {
        o["result"] = result;
        o["failed"] = failed;
        if (failed)
            o["error"] = errorMsg;
    }
    return o;
}

ToolCall ToolCall::fromJson(const QJsonObject &obj)
{
    ToolCall tc;
    tc.id = obj.value("id").toString();
    tc.name = obj.value("name").toString();
    tc.args = obj.value("args").toObject();
    return tc;
}

}  // namespace qcai2
