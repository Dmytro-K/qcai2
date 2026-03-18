/*! Declares provider-specific raw-event adapters that normalize progress
 *  updates into a shared internal event model.
 */
#pragma once

#include "AgentProgress.h"

#include <QList>

#include <memory>

namespace qcai2
{

class i_provider_progress_adapter_t
{
public:
    virtual ~i_provider_progress_adapter_t() = default;
    virtual QList<agent_progress_event_t> adapt(const provider_raw_event_t &event) = 0;
};

std::unique_ptr<i_provider_progress_adapter_t>
create_provider_progress_adapter(const QString &provider_id);

}  // namespace qcai2
