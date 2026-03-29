/*! Implements shared provider token-usage metadata helpers. */

#include "provider_usage.h"

#include "../util/json.h"

#include <QStringList>

namespace qcai2
{

namespace
{

int first_non_negative(std::initializer_list<int> values)
{
    for (const int value : values)
    {
        if (value >= 0)
        {
            return value;
        }
    }
    return -1;
}

QString format_duration_part(std::int64_t duration_ms)
{
    if (duration_ms < 0)
    {
        return {};
    }
    if (duration_ms < 1000)
    {
        return QStringLiteral("time %1 ms").arg(duration_ms);
    }

    const double seconds = static_cast<double>(duration_ms) / 1000.0;
    if (seconds < 10.0)
    {
        return QStringLiteral("time %1 s").arg(QString::number(seconds, 'f', 2));
    }
    if (seconds < 60.0)
    {
        return QStringLiteral("time %1 s").arg(QString::number(seconds, 'f', 1));
    }

    const qint64 totalSeconds = duration_ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 remaining_seconds = totalSeconds % 60;
    return QStringLiteral("time %1m %2s")
        .arg(minutes)
        .arg(remaining_seconds, 2, 10, QLatin1Char('0'));
}

}  // namespace

bool provider_usage_t::has_any() const
{
    return input_tokens >= 0 || output_tokens >= 0 || total_tokens >= 0 || reasoning_tokens >= 0 ||
           cached_input_tokens >= 0;
}

int provider_usage_t::resolved_total_tokens() const
{
    if (((total_tokens >= 0) == true))
    {
        return total_tokens;
    }
    if (((input_tokens >= 0 && output_tokens >= 0) == true))
    {
        return input_tokens + output_tokens;
    }
    return -1;
}

int provider_usage_t::uncached_input_tokens() const
{
    if (input_tokens < 0)
    {
        return -1;
    }

    return qMax(0, input_tokens - qMax(cached_input_tokens, 0));
}

provider_usage_t provider_usage_from_response_object(const QJsonObject &response_object)
{
    QJsonObject usage_object = response_object.value(QStringLiteral("usage")).toObject();
    if (usage_object.isEmpty() == true)
    {
        usage_object = response_object;
    }

    provider_usage_t usage;
    usage.input_tokens =
        first_non_negative({json::get_int(usage_object, QStringLiteral("input_tokens"), -1),
                            json::get_int(usage_object, QStringLiteral("prompt_tokens"), -1)});
    usage.output_tokens =
        first_non_negative({json::get_int(usage_object, QStringLiteral("output_tokens"), -1),
                            json::get_int(usage_object, QStringLiteral("completion_tokens"), -1)});
    usage.total_tokens = json::get_int(usage_object, QStringLiteral("total_tokens"), -1);
    usage.reasoning_tokens = first_non_negative(
        {json::get_int(usage_object, QStringLiteral("reasoning_tokens"), -1),
         json::get_int(usage_object, QStringLiteral("output_tokens_details/reasoning_tokens"), -1),
         json::get_int(usage_object, QStringLiteral("completion_tokens_details/reasoning_tokens"),
                       -1)});
    usage.cached_input_tokens = first_non_negative(
        {json::get_int(usage_object, QStringLiteral("cached_input_tokens"), -1),
         json::get_int(usage_object, QStringLiteral("input_tokens_details/cached_tokens"), -1),
         json::get_int(usage_object, QStringLiteral("prompt_tokens_details/cached_tokens"), -1)});

    // Extract optional Copilot quota snapshot
    const QJsonObject quota_obj = response_object.value(QStringLiteral("quota")).toObject();
    if (!quota_obj.isEmpty())
    {
        const double pct = quota_obj.value(QStringLiteral("remaining_percentage")).toDouble(-1.0);
        if (pct >= 0.0)
        {
            usage.quota_remaining_percent = pct;
        }
        const int used = quota_obj.value(QStringLiteral("used_requests")).toInt(-1);
        if (used >= 0)
        {
            usage.quota_used_requests = used;
        }
        const int total = quota_obj.value(QStringLiteral("entitlement_requests")).toInt(-1);
        if (total >= 0)
        {
            usage.quota_total_requests = total;
        }
    }

    return usage;
}

provider_usage_t accumulate_provider_usage(const provider_usage_t &lhs,
                                           const provider_usage_t &rhs)
{
    provider_usage_t usage;
    usage.input_tokens = (lhs.input_tokens >= 0 || rhs.input_tokens >= 0)
                             ? qMax(0, qMax(lhs.input_tokens, 0) + qMax(rhs.input_tokens, 0))
                             : -1;
    usage.output_tokens = (lhs.output_tokens >= 0 || rhs.output_tokens >= 0)
                              ? qMax(0, qMax(lhs.output_tokens, 0) + qMax(rhs.output_tokens, 0))
                              : -1;
    usage.reasoning_tokens =
        (lhs.reasoning_tokens >= 0 || rhs.reasoning_tokens >= 0)
            ? qMax(0, qMax(lhs.reasoning_tokens, 0) + qMax(rhs.reasoning_tokens, 0))
            : -1;
    usage.cached_input_tokens =
        (lhs.cached_input_tokens >= 0 || rhs.cached_input_tokens >= 0)
            ? qMax(0, qMax(lhs.cached_input_tokens, 0) + qMax(rhs.cached_input_tokens, 0))
            : -1;
    usage.total_tokens =
        (lhs.resolved_total_tokens() >= 0 || rhs.resolved_total_tokens() >= 0)
            ? qMax(0, qMax(lhs.resolved_total_tokens(), 0) + qMax(rhs.resolved_total_tokens(), 0))
            : -1;
    // Quota is a point-in-time snapshot; keep the latest (rhs wins if available).
    usage.quota_remaining_percent = rhs.quota_remaining_percent >= 0.0
                                        ? rhs.quota_remaining_percent
                                        : lhs.quota_remaining_percent;
    usage.quota_used_requests =
        rhs.quota_used_requests >= 0 ? rhs.quota_used_requests : lhs.quota_used_requests;
    usage.quota_total_requests =
        rhs.quota_total_requests >= 0 ? rhs.quota_total_requests : lhs.quota_total_requests;
    return usage;
}

QString format_provider_usage_summary(const provider_usage_t &usage, std::int64_t duration_ms)
{
    if ((usage.has_any() == false) && (duration_ms < 0))
    {
        return {};
    }

    QStringList parts;
    if (((usage.input_tokens >= 0) == true))
    {
        parts.append(QStringLiteral("input %1").arg(usage.input_tokens));
    }
    if (((usage.output_tokens >= 0) == true))
    {
        parts.append(QStringLiteral("output %1").arg(usage.output_tokens));
    }

    const int total_tokens = usage.resolved_total_tokens();
    if (((total_tokens >= 0) == true))
    {
        parts.append(QStringLiteral("total %1").arg(total_tokens));
    }
    if (((usage.reasoning_tokens >= 0) == true))
    {
        parts.append(QStringLiteral("reasoning %1").arg(usage.reasoning_tokens));
    }
    if (((usage.cached_input_tokens >= 0) == true))
    {
        parts.append(QStringLiteral("cached input %1").arg(usage.cached_input_tokens));
    }

    const QString duration_part = format_duration_part(duration_ms);
    if (duration_part.isEmpty() == false)
    {
        parts.append(duration_part);
    }

    return parts.join(QStringLiteral(" | "));
}

}  // namespace qcai2
