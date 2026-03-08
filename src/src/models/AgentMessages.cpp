/*! Implements parsing helpers for chat messages and structured agent responses. */
#include "AgentMessages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace qcai2
{

namespace
{

/**
 * Extracts top-level JSON objects from mixed text while respecting strings and escapes.
 * @param raw Raw completion text.
 */
QList<QString> extractTopLevelObjects(const QString &raw)
{
    QList<QString> objects;
    int start = -1;
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (int i = 0; i < raw.size(); ++i)
    {
        const QChar ch = raw.at(i);

        if (inString)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (ch == QLatin1Char('\\'))
            {
                escaped = true;
            }
            else if (ch == QLatin1Char('"'))
            {
                inString = false;
            }
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            inString = true;
            continue;
        }

        if (ch == QLatin1Char('{'))
        {
            if (depth == 0)
                start = i;
            ++depth;
            continue;
        }

        if (ch == QLatin1Char('}') && depth > 0)
        {
            --depth;
            if (depth == 0 && start >= 0)
            {
                objects.append(raw.mid(start, i - start + 1));
                start = -1;
            }
        }
    }

    return objects;
}

}  // namespace

// ---------------------------------------------------------------------------
// ChatMessage
// ---------------------------------------------------------------------------

QJsonObject ChatMessage::toJson() const
{
    return QJsonObject{{"role", role}, {"content", content}};
}

ChatMessage ChatMessage::fromJson(const QJsonObject &obj)
{
    return {obj.value("role").toString(), obj.value("content").toString()};
}

// ---------------------------------------------------------------------------
// AgentResponse
// ---------------------------------------------------------------------------

AgentResponse AgentResponse::parseJson(const QJsonObject &obj)
{
    AgentResponse r;
    const QString type = obj.value("type").toString();

    if (type == "plan")
    {
        r.type = ResponseType::Plan;
        const QJsonArray arr = obj.value("steps").toArray();
        for (int i = 0; i < arr.size(); ++i)
        {
            PlanStep s;
            s.index = i;
            s.description = arr[i].isString() ? arr[i].toString()
                                              : arr[i].toObject().value("description").toString();
            r.steps.append(s);
        }
        return r;
    }

    if (type == "tool_call")
    {
        r.type = ResponseType::ToolCall;
        r.toolName = obj.value("name").toString();
        r.toolArgs = obj.value("args").toObject();
        return r;
    }

    if (type == "final")
    {
        r.type = ResponseType::Final;
        r.summary = obj.value("summary").toString();
        r.diff = obj.value("diff").toString();
        return r;
    }

    if (type == "need_approval")
    {
        r.type = ResponseType::NeedApproval;
        r.approvalAction = obj.value("action").toString();
        r.approvalReason = obj.value("reason").toString();
        r.approvalPreview = obj.value("preview").toString();
        return r;
    }

    // Unknown type – treat as text
    r.type = ResponseType::Text;
    r.text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return r;
}

AgentResponse AgentResponse::parse(const QString &raw)
{
    const auto tryParseTypedResponse = [](const QString &candidate, AgentResponse &out) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return false;

        const QJsonObject obj = doc.object();
        if (!obj.value(QStringLiteral("type")).isString())
            return false;

        out = parseJson(obj);
        return true;

    };

    AgentResponse parsed;
    if (tryParseTypedResponse(raw, parsed))
        return parsed;

    // Keep previous behavior: if the whole payload is a single JSON object,
    // return parseJson() even for unknown/unsupported type.
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject())
        return parseJson(doc.object());

    static const QRegularExpression fencedJsonRe(R"(```(?:json)?\s*(\{[\s\S]*?\})\s*```)",
                                                 QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = fencedJsonRe.globalMatch(raw);
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        if (tryParseTypedResponse(m.captured(1), parsed))
            return parsed;
    }

    // Handle multiple JSON objects in one message, e.g.
    // {"type":"tool_call",...}{"type":"final",...}
    const QList<QString> objects = extractTopLevelObjects(raw);
    for (const QString &candidate : objects)
    {
        if (tryParseTypedResponse(candidate, parsed))
            return parsed;
    }

    // Fallback: return raw text
    AgentResponse r;
    r.type = ResponseType::Text;
    r.text = raw;
    return r;
}

}  // namespace qcai2
