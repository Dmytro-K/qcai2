/*! Implements queued-request helpers used by the agent dock. */

#include "request_queue.h"

#include <QRegularExpression>

namespace qcai2
{

namespace
{

QString run_mode_label(agent_controller_t::run_mode_t run_mode)
{
    return run_mode == agent_controller_t::run_mode_t::ASK ? QStringLiteral("Ask")
                                                           : QStringLiteral("Agent");
}

QString run_mode_storage_value(agent_controller_t::run_mode_t run_mode)
{
    return run_mode == agent_controller_t::run_mode_t::ASK ? QStringLiteral("ask")
                                                           : QStringLiteral("agent");
}

agent_controller_t::run_mode_t run_mode_from_storage_value(const QString &value)
{
    return value == QStringLiteral("ask") ? agent_controller_t::run_mode_t::ASK
                                          : agent_controller_t::run_mode_t::AGENT;
}

QString compact_goal_preview(const QString &goal)
{
    QString preview = goal;
    preview.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    preview = preview.trimmed();
    if (preview.size() > 96)
    {
        preview = preview.left(93).trimmed() + QStringLiteral("...");
    }
    return preview;
}

}  // namespace

bool queued_request_t::is_valid() const
{
    return this->goal.trimmed().isEmpty() == false;
}

QString queued_request_t::display_text() const
{
    QString text = QStringLiteral("%1 — %2").arg(run_mode_label(this->run_mode),
                                                 compact_goal_preview(this->goal));
    if (this->linked_files.isEmpty() == false)
    {
        text += QStringLiteral("  [%1 linked]").arg(this->linked_files.size());
    }
    return text;
}

QJsonObject queued_request_t::to_json() const
{
    return QJsonObject{
        {QStringLiteral("goal"), this->goal},
        {QStringLiteral("requestContext"), this->request_context},
        {QStringLiteral("linkedFiles"), QJsonArray::fromStringList(this->linked_files)},
        {QStringLiteral("dryRun"), this->dry_run},
        {QStringLiteral("runMode"), run_mode_storage_value(this->run_mode)},
        {QStringLiteral("model"), this->model_name},
        {QStringLiteral("reasoningEffort"), this->reasoning_effort},
        {QStringLiteral("thinkingLevel"), this->thinking_level}};
}

queued_request_t queued_request_t::from_json(const QJsonObject &obj)
{
    queued_request_t request;
    request.goal = obj.value(QStringLiteral("goal")).toString();
    request.request_context = obj.value(QStringLiteral("requestContext")).toString();
    for (const QJsonValue &value : obj.value(QStringLiteral("linkedFiles")).toArray())
    {
        if (value.isString() == true && value.toString().trimmed().isEmpty() == false)
        {
            request.linked_files.append(value.toString().trimmed());
        }
    }
    request.dry_run = obj.value(QStringLiteral("dryRun")).toBool(true);
    request.run_mode =
        run_mode_from_storage_value(obj.value(QStringLiteral("runMode")).toString());
    request.model_name = obj.value(QStringLiteral("model")).toString();
    request.reasoning_effort = obj.value(QStringLiteral("reasoningEffort")).toString();
    request.thinking_level = obj.value(QStringLiteral("thinkingLevel")).toString();
    return request;
}

bool request_queue_t::enqueue(const queued_request_t &request)
{
    if (request.is_valid() == false)
    {
        return false;
    }

    this->queued_requests.append(request);
    return true;
}

bool request_queue_t::take_next(queued_request_t *request)
{
    if (request == nullptr || this->queued_requests.isEmpty() == true)
    {
        return false;
    }

    *request = this->queued_requests.takeFirst();
    return true;
}

void request_queue_t::clear()
{
    this->queued_requests.clear();
}

bool request_queue_t::is_empty() const
{
    return this->queued_requests.isEmpty();
}

int request_queue_t::size() const
{
    return static_cast<int>(this->queued_requests.size());
}

QList<queued_request_t> request_queue_t::items() const
{
    return this->queued_requests;
}

QJsonArray request_queue_t::to_json() const
{
    QJsonArray array;
    for (const queued_request_t &request : this->queued_requests)
    {
        array.append(request.to_json());
    }
    return array;
}

void request_queue_t::restore(const QJsonValue &value)
{
    this->queued_requests.clear();
    if (value.isArray() == false)
    {
        return;
    }

    for (const QJsonValue &entry : value.toArray())
    {
        if (entry.isObject() == false)
        {
            continue;
        }

        const queued_request_t request = queued_request_t::from_json(entry.toObject());
        if (request.is_valid() == true)
        {
            this->queued_requests.append(request);
        }
    }
}

}  // namespace qcai2
