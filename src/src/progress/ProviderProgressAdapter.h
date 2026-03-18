/*! Declares provider-specific raw-event adapters that normalize progress
 *  updates into a shared internal event model.
 */
#pragma once

#include "AgentProgress.h"

#include <QList>

#include <memory>

namespace qcai2
{

class IProviderProgressAdapter
{
public:
    virtual ~IProviderProgressAdapter() = default;
    virtual QList<AgentProgressEvent> adapt(const ProviderRawEvent &event) = 0;
};

std::unique_ptr<IProviderProgressAdapter> createProviderProgressAdapter(const QString &providerId);

}  // namespace qcai2
