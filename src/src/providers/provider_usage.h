/*! Declares shared provider token-usage metadata helpers. */
#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

#include <cstdint>

namespace qcai2
{

/**
 * Token-usage counters reported by model providers for one completion request.
 */
struct provider_usage_t
{
    int input_tokens = -1;
    int output_tokens = -1;
    int total_tokens = -1;
    int reasoning_tokens = -1;
    int cached_input_tokens = -1;

    // Optional Copilot premium-request quota snapshot (−1 = unavailable)
    double quota_remaining_percent = -1.0;
    int quota_used_requests = -1;
    int quota_total_requests = -1;

    bool has_any() const;
    int resolved_total_tokens() const;
    int uncached_input_tokens() const;
};

provider_usage_t provider_usage_from_response_object(const QJsonObject &response_object);
provider_usage_t accumulate_provider_usage(const provider_usage_t &lhs,
                                           const provider_usage_t &rhs);
QString format_provider_usage_summary(const provider_usage_t &usage,
                                      std::int64_t duration_ms = -1);

}  // namespace qcai2

Q_DECLARE_METATYPE(qcai2::provider_usage_t)
