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

QString extract_partial_json_string_field(const QString &raw, const QString &field_name)
{
    const QRegularExpression field_re(
        QStringLiteral(R"("%1"\s*:\s*")").arg(QRegularExpression::escape(field_name)));
    const QRegularExpressionMatch match = field_re.match(raw);
    if (match.hasMatch() == false)
    {
        return {};
    }

    QString value;
    bool escaping = false;
    for (qsizetype i = match.capturedEnd(); i < raw.size(); ++i)
    {
        const QChar ch = raw.at(i);
        if (escaping == true)
        {
            escaping = false;
            switch (ch.unicode())
            {
                case '"':
                    value += QLatin1Char('"');
                    break;
                case '\\':
                    value += QLatin1Char('\\');
                    break;
                case '/':
                    value += QLatin1Char('/');
                    break;
                case 'b':
                    value += QLatin1Char('\b');
                    break;
                case 'f':
                    value += QLatin1Char('\f');
                    break;
                case 'n':
                    value += QLatin1Char('\n');
                    break;
                case 'r':
                    value += QLatin1Char('\r');
                    break;
                case 't':
                    value += QLatin1Char('\t');
                    break;
                case 'u': {
                    if (i + 4 >= raw.size())
                    {
                        return value;
                    }
                    bool ok = false;
                    const ushort code = raw.sliced(i + 1, 4).toUShort(&ok, 16);
                    if (ok == false)
                    {
                        return value;
                    }
                    value += QChar(code);
                    i += 4;
                    break;
                }
                default:
                    value += ch;
                    break;
            }
            continue;
        }

        if (ch == QLatin1Char('\\'))
        {
            escaping = true;
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            return value;
        }

        value += ch;
    }

    return value;
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
// agent_decision_option_t / agent_decision_request_t
// ---------------------------------------------------------------------------

QJsonObject agent_decision_option_t::to_json() const
{
    return QJsonObject{{QStringLiteral("id"), this->id},
                       {QStringLiteral("label"), this->label},
                       {QStringLiteral("description"), this->description}};
}

agent_decision_option_t agent_decision_option_t::from_json(const QJsonObject &obj)
{
    agent_decision_option_t option;
    option.id = obj.value(QStringLiteral("id")).toString();
    option.label = obj.value(QStringLiteral("label")).toString();
    option.description = obj.value(QStringLiteral("description")).toString();
    return option;
}

QJsonObject agent_decision_request_t::to_json() const
{
    QJsonArray options_json;
    for (const agent_decision_option_t &option : this->options)
    {
        options_json.append(option.to_json());
    }

    return QJsonObject{{QStringLiteral("request_id"), this->request_id},
                       {QStringLiteral("title"), this->title},
                       {QStringLiteral("description"), this->description},
                       {QStringLiteral("options"), options_json},
                       {QStringLiteral("allow_freeform"), this->allow_freeform},
                       {QStringLiteral("freeform_placeholder"), this->freeform_placeholder},
                       {QStringLiteral("recommended_option_id"), this->recommended_option_id}};
}

agent_decision_request_t agent_decision_request_t::from_json(const QJsonObject &obj)
{
    agent_decision_request_t request;
    request.request_id = obj.value(QStringLiteral("request_id")).toString();
    request.title = obj.value(QStringLiteral("title")).toString();
    request.description = obj.value(QStringLiteral("description")).toString();
    request.allow_freeform = obj.value(QStringLiteral("allow_freeform")).toBool(false);
    request.freeform_placeholder = obj.value(QStringLiteral("freeform_placeholder")).toString();
    request.recommended_option_id = obj.value(QStringLiteral("recommended_option_id")).toString();

    const QJsonArray options_json = obj.value(QStringLiteral("options")).toArray();
    request.options.reserve(options_json.size());
    for (const QJsonValue &value : options_json)
    {
        if (value.isObject() == false)
        {
            continue;
        }
        request.options.append(agent_decision_option_t::from_json(value.toObject()));
    }
    return request;
}

int agent_decision_request_t::option_index(const QString &option_id) const
{
    const QString trimmed_option_id = option_id.trimmed();
    if (trimmed_option_id.isEmpty() == true)
    {
        return -1;
    }

    for (int i = 0; i < this->options.size(); ++i)
    {
        if (this->options.at(i).id == trimmed_option_id)
        {
            return i;
        }
    }
    return -1;
}

bool agent_decision_request_t::is_valid() const
{
    return this->options.isEmpty() == false || this->allow_freeform == true;
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

    if (type == "decision_request")
    {
        r.type = response_type_t::DECISION_REQUEST;
        r.decision_request = agent_decision_request_t::from_json(obj);
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

QString streaming_response_markdown_preview(const QString &raw)
{
    if (raw.trimmed().isEmpty() == true)
    {
        return {};
    }

    const QString trimmed = raw.trimmed();
    const bool looks_like_json = trimmed.startsWith(QLatin1Char('{'));

    if (looks_like_json == false)
    {
        return raw;
    }

    const agent_response_t parsed = agent_response_t::parse(raw);
    if (parsed.type == response_type_t::FINAL)
    {
        return parsed.summary;
    }
    if (parsed.type != response_type_t::TEXT && parsed.type != response_type_t::ERROR)
    {
        return {};
    }

    const QString partial_type = extract_partial_json_string_field(raw, QStringLiteral("type"));
    if (partial_type.isEmpty() == false && partial_type != QStringLiteral("final"))
    {
        return {};
    }

    return extract_partial_json_string_field(raw, QStringLiteral("summary"));
}

}  // namespace qcai2
