/*! Declares shared provider token-usage metadata helpers. */
#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace qcai2
{

/**
 * Token-usage counters reported by model providers for one completion request.
 */
struct ProviderUsage
{
    int inputTokens = -1;
    int outputTokens = -1;
    int totalTokens = -1;
    int reasoningTokens = -1;
    int cachedInputTokens = -1;

    bool hasAny() const;
    int resolvedTotalTokens() const;
};

ProviderUsage providerUsageFromResponseObject(const QJsonObject &responseObject);
QString formatProviderUsageSummary(const ProviderUsage &usage);

}  // namespace qcai2

Q_DECLARE_METATYPE(qcai2::ProviderUsage)
