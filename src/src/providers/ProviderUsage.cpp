/*! Implements shared provider token-usage metadata helpers. */

#include "ProviderUsage.h"

#include "../util/Json.h"

#include <QStringList>

namespace qcai2
{

namespace
{

int firstNonNegative(std::initializer_list<int> values)
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

QString formatDurationPart(std::int64_t durationMs)
{
    if (durationMs < 0)
    {
        return {};
    }
    if (durationMs < 1000)
    {
        return QStringLiteral("time %1 ms").arg(durationMs);
    }

    const double seconds = static_cast<double>(durationMs) / 1000.0;
    if (seconds < 10.0)
    {
        return QStringLiteral("time %1 s").arg(QString::number(seconds, 'f', 2));
    }
    if (seconds < 60.0)
    {
        return QStringLiteral("time %1 s").arg(QString::number(seconds, 'f', 1));
    }

    const qint64 totalSeconds = durationMs / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 remainingSeconds = totalSeconds % 60;
    return QStringLiteral("time %1m %2s")
        .arg(minutes)
        .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

}  // namespace

bool ProviderUsage::hasAny() const
{
    return inputTokens >= 0 || outputTokens >= 0 || totalTokens >= 0 || reasoningTokens >= 0 ||
           cachedInputTokens >= 0;
}

int ProviderUsage::resolvedTotalTokens() const
{
    if (((totalTokens >= 0) == true))
    {
        return totalTokens;
    }
    if (((inputTokens >= 0 && outputTokens >= 0) == true))
    {
        return inputTokens + outputTokens;
    }
    return -1;
}

ProviderUsage providerUsageFromResponseObject(const QJsonObject &responseObject)
{
    QJsonObject usageObject = responseObject.value(QStringLiteral("usage")).toObject();
    if (usageObject.isEmpty() == true)
    {
        usageObject = responseObject;
    }

    ProviderUsage usage;
    usage.inputTokens =
        firstNonNegative({Json::getInt(usageObject, QStringLiteral("input_tokens"), -1),
                          Json::getInt(usageObject, QStringLiteral("prompt_tokens"), -1)});
    usage.outputTokens =
        firstNonNegative({Json::getInt(usageObject, QStringLiteral("output_tokens"), -1),
                          Json::getInt(usageObject, QStringLiteral("completion_tokens"), -1)});
    usage.totalTokens = Json::getInt(usageObject, QStringLiteral("total_tokens"), -1);
    usage.reasoningTokens = firstNonNegative(
        {Json::getInt(usageObject, QStringLiteral("reasoning_tokens"), -1),
         Json::getInt(usageObject, QStringLiteral("output_tokens_details/reasoning_tokens"), -1),
         Json::getInt(usageObject, QStringLiteral("completion_tokens_details/reasoning_tokens"),
                      -1)});
    usage.cachedInputTokens = firstNonNegative(
        {Json::getInt(usageObject, QStringLiteral("cached_input_tokens"), -1),
         Json::getInt(usageObject, QStringLiteral("input_tokens_details/cached_tokens"), -1),
         Json::getInt(usageObject, QStringLiteral("prompt_tokens_details/cached_tokens"), -1)});
    return usage;
}

QString formatProviderUsageSummary(const ProviderUsage &usage, std::int64_t durationMs)
{
    if ((usage.hasAny() == false) && (durationMs < 0))
    {
        return {};
    }

    QStringList parts;
    if (((usage.inputTokens >= 0) == true))
    {
        parts.append(QStringLiteral("input %1").arg(usage.inputTokens));
    }
    if (((usage.outputTokens >= 0) == true))
    {
        parts.append(QStringLiteral("output %1").arg(usage.outputTokens));
    }

    const int totalTokens = usage.resolvedTotalTokens();
    if (((totalTokens >= 0) == true))
    {
        parts.append(QStringLiteral("total %1").arg(totalTokens));
    }
    if (((usage.reasoningTokens >= 0) == true))
    {
        parts.append(QStringLiteral("reasoning %1").arg(usage.reasoningTokens));
    }
    if (((usage.cachedInputTokens >= 0) == true))
    {
        parts.append(QStringLiteral("cached input %1").arg(usage.cachedInputTokens));
    }

    const QString durationPart = formatDurationPart(durationMs);
    if (durationPart.isEmpty() == false)
    {
        parts.append(durationPart);
    }

    return parts.join(QStringLiteral(" | "));
}

}  // namespace qcai2
