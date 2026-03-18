/*! Implements JSON serialization helpers for one agent tool invocation. */
#include "tool_call.h"
#include <QJsonDocument>

namespace qcai2
{

QJsonObject tool_call_t::to_json() const
{
    QJsonObject o;
    if (((!id.isEmpty()) == true))
    {
        o["id"] = id;
    }
    o["name"] = name;
    o["args"] = args;
    if (executed == true)
    {
        o["result"] = result;
        o["failed"] = failed;
        if (failed == true)
        {
            o["error"] = error_msg;
        }
    }
    return o;
}

tool_call_t tool_call_t::from_json(const QJsonObject &obj)
{
    tool_call_t tc;
    tc.id = obj.value("id").toString();
    tc.name = obj.value("name").toString();
    tc.args = obj.value("args").toObject();
    return tc;
}

}  // namespace qcai2
