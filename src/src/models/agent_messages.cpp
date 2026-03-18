/*! Implements parsing helpers for chat messages and structured agent responses. */
#include "agent_messages.h"

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
QList<QString> extract_top_level_objects(const QString &raw)
{
    QList<QString> objects;
    int start = -1;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (int i = 0; i < raw.size(); ++i)
    {
        const QChar ch = raw.at(i);

        if (in_string == true)
        {
            if (escaped == true)
            {
                escaped = false;
            }
            else if (ch == QLatin1Char('\\'))
            {
                escaped = true;
            }
            else if (ch == QLatin1Char('"'))
            {
                in_string = false;
            }
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            in_string = true;
            continue;
        }

        if (ch == QLatin1Char('{'))
        {
            if (((depth == 0) == true))
            {
                start = i;
            }
            ++depth;
            continue;
        }

        if (ch == QLatin1Char('}') && depth > 0)
        {
            --depth;
            if (((depth == 0 && start >= 0) == true))
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
// chat_message_t
// ---------------------------------------------------------------------------

QJsonObject chat_message_t::to_json() const
{
    return QJsonObject{{"role", role}, {"content", content}};
}

chat_message_t chat_message_t::from_json(const QJsonObject &obj)
{
    return {obj.value("role").toString(), obj.value("content").toString()};
}

// ---------------------------------------------------------------------------
// agent_response_t
// ---------------------------------------------------------------------------

agent_response_t agent_response_t::parse_json(const QJsonObject &obj)
{
    agent_response_t r;
    const QString type = obj.value("type").toString();

    if (type == "plan")
    {
        r.type = response_type_t::PLAN;
        const QJsonArray arr = obj.value("steps").toArray();
        for (int i = 0; i < arr.size(); ++i)
        {
            plan_step_t s;
            s.index = i;
            s.description = arr[i].isString() ? arr[i].toString()
                                              : arr[i].toObject().value("description").toString();
            r.steps.append(s);
        }
        return r;
    }

    if (type == "tool_call")
    {
        r.type = response_type_t::TOOL_CALL;
        r.tool_name = obj.value("name").toString();
        r.tool_args = obj.value("args").toObject();
        return r;
    }

    if (type == "final")
    {
        r.type = response_type_t::FINAL;
        r.summary = obj.value("summary").toString();
        r.diff = obj.value("diff").toString();
        return r;
    }

    if (type == "need_approval")
    {
        r.type = response_type_t::NEED_APPROVAL;
        r.approval_action = obj.value("action").toString();
        r.approval_reason = obj.value("reason").toString();
        r.approval_preview = obj.value("preview").toString();
        return r;
    }

    // Unknown type – treat as text
    r.type = response_type_t::TEXT;
    r.text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return r;
}

agent_response_t agent_response_t::parse(const QString &raw)
{
    const auto try_parse_typed_response = [](const QString &candidate, agent_response_t &out) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }

        const QJsonObject obj = doc.object();
        if (!obj.value(QStringLiteral("type")).isString())
        {
            return false;
        }

        out = parse_json(obj);
        return true;
    };

    agent_response_t parsed;
    if (try_parse_typed_response(raw, parsed))
    {
        return parsed;
    }

    // Keep previous behavior: if the whole payload is a single JSON object,
    // return parse_json() even for unknown/unsupported type.
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject())
    {
        return parse_json(doc.object());
    }

    static const QRegularExpression fenced_json_re(R"(```(?:json)?\s*(\{[\s\S]*?\})\s*```)",
                                                   QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = fenced_json_re.globalMatch(raw);
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        if (try_parse_typed_response(m.captured(1), parsed))
        {
            return parsed;
        }
    }

    // Handle multiple JSON objects in one message, e.g.
    // {"type":"tool_call",...}{"type":"final",...}
    const QList<QString> objects = extract_top_level_objects(raw);
    for (const QString &candidate : objects)
    {
        if (try_parse_typed_response(candidate, parsed))
        {
            return parsed;
        }
    }

    // Fallback: return raw text
    agent_response_t r;
    r.type = response_type_t::TEXT;
    r.text = raw;
    return r;
}

}  // namespace qcai2
