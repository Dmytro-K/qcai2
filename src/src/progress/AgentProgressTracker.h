/*! Declares the progress tracker that glues provider raw events, normalized
 *  events, semantic status classification, and rendering together.
 */
#pragma once

#include "AgentStatusRenderer.h"
#include "ProviderProgressAdapter.h"

#include <QStringList>

#include <memory>

namespace qcai2
{

struct AgentProgressRenderResult
{
    bool statusChanged = false;
    QString statusText;
    QString stableLogLine;
    QStringList debugLines;
};

class AgentProgressTracker
{
public:
    AgentProgressTracker(const QString &providerId, AgentStatusRenderMode mode, bool verbose);

    AgentProgressRenderResult handleProviderRawEvent(const ProviderRawEvent &event);
    AgentProgressRenderResult handleNormalizedEvent(const AgentProgressEvent &event);
    QString currentStatusText() const;

private:
    AgentProgressRenderResult merge(const AgentProgressRenderResult &lhs,
                                    const AgentProgressRenderResult &rhs) const;
    AgentProgressRenderResult applyNormalizedEvent(const AgentProgressEvent &event,
                                                   const ProviderRawEvent *rawEvent);

    std::unique_ptr<IProviderProgressAdapter> m_adapter;
    AgentStatusClassifier m_classifier;
    AgentStatusRenderer m_renderer;
    bool m_verbose = false;
};

}  // namespace qcai2
