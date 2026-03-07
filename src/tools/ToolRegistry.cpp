#include "ToolRegistry.h"
#include "../util/Logger.h"

#include <QJsonArray>
#include <QJsonObject>

namespace qcai2
{

ToolRegistry::ToolRegistry(QObject *parent) : QObject(parent)
{
}

void ToolRegistry::registerTool(std::shared_ptr<ITool> tool)
{
    QCAI_DEBUG("Tools", QStringLiteral("Registered tool: %1").arg(tool->name()));
    m_tools.insert(tool->name(), std::move(tool));
}

ITool *ToolRegistry::tool(const QString &name) const
{
    auto it = m_tools.find(name);
    return (it != m_tools.end()) ? it->get() : nullptr;
}

QList<std::shared_ptr<ITool>> ToolRegistry::allTools() const
{
    return m_tools.values();
}

QJsonArray ToolRegistry::toolDescriptionsJson() const
{
    QJsonArray arr;
    for (auto it = m_tools.begin(); it != m_tools.end(); ++it)
    {
        QJsonObject desc;
        desc["name"] = it.value()->name();
        desc["description"] = it.value()->description();
        desc["args"] = it.value()->argsSchema();
        arr.append(desc);
    }
    return arr;
}

}  // namespace qcai2
