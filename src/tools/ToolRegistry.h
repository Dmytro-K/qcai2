#pragma once

#include "ITool.h"

#include <QObject>
#include <QMap>
#include <QList>
#include <memory>

namespace Qcai2 {

// Central registry of available tools.
class ToolRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ToolRegistry(QObject *parent = nullptr);

    void registerTool(std::shared_ptr<ITool> tool);
    ITool *tool(const QString &name) const;
    QList<std::shared_ptr<ITool>> allTools() const;

    // Build a JSON array describing all tools (for inclusion in LLM system prompt)
    QJsonArray toolDescriptionsJson() const;

private:
    QMap<QString, std::shared_ptr<ITool>> m_tools;
};

} // namespace Qcai2
