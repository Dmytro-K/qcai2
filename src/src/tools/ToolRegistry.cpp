#include "ToolRegistry.h"
#include "../util/Logger.h"

#include <QJsonArray>
#include <QJsonObject>

namespace qcai2
{

/**
 * Creates an empty tool registry.
 * @param parent Parent QObject that owns this instance.
 */
ToolRegistry::ToolRegistry(QObject *parent) : QObject(parent)
{
}

/**
 * Registers or replaces a tool using its stable name as the key.
 * @param tool Tool instance to register.
 */
void ToolRegistry::registerTool(const std::shared_ptr<ITool> &tool)
{
    QCAI_DEBUG("Tools", QStringLiteral("Registered tool: %1").arg(tool->name()));
    m_tools.insert(tool->name(), tool);
}

/**
 * Looks up a registered tool by name.
 * @param name Name value.
 */
ITool *ToolRegistry::tool(const QString &name) const
{
    auto it = m_tools.find(name);
    return (it != m_tools.end()) ? it->get() : nullptr;
}

bool ToolRegistry::unregisterTool(const QString &name)
{
    const auto removed = m_tools.remove(name);
    if (removed > 0)
    {
        QCAI_DEBUG("Tools", QStringLiteral("Unregistered tool: %1").arg(name));
    }
    return removed > 0;
}

/**
 * Returns all registered tools.
 */
QList<std::shared_ptr<ITool>> ToolRegistry::allTools() const
{
    return m_tools.values();
}

/**
 * Serializes registered tool metadata for the agent prompt.
 */
QJsonArray ToolRegistry::toolDescriptionsJson() const
{
    QJsonArray arr;
    for (auto it = m_tools.begin(); ((it != m_tools.end()) == true); ++it)
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
