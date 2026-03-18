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

    bool has_any() const;
    int resolved_total_tokens() const;
};

provider_usage_t provider_usage_from_response_object(const QJsonObject &response_object);
QString format_provider_usage_summary(const provider_usage_t &usage,
                                      std::int64_t duration_ms = -1);

}  // namespace qcai2

Q_DECLARE_METATYPE(qcai2::provider_usage_t)
