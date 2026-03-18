#include "tool_registry.h"
#include "../util/logger.h"

#include <QJsonArray>
#include <QJsonObject>

namespace qcai2
{

/**
 * Creates an empty tool registry.
 * @param parent Parent QObject that owns this instance.
 */
tool_registry_t::tool_registry_t(QObject *parent) : QObject(parent)
{
}

/**
 * Registers or replaces a tool using its stable name as the key.
 * @param tool Tool instance to register.
 */
void tool_registry_t::register_tool(const std::shared_ptr<i_tool_t> &tool)
{
    QCAI_DEBUG("Tools", QStringLiteral("Registered tool: %1").arg(tool->name()));
    this->tools.insert(tool->name(), tool);
}

/**
 * Looks up a registered tool by name.
 * @param name Name value.
 */
i_tool_t *tool_registry_t::tool(const QString &name) const
{
    auto it = this->tools.find(name);
    return (it != this->tools.end()) ? it->get() : nullptr;
}

bool tool_registry_t::unregister_tool(const QString &name)
{
    const auto removed = this->tools.remove(name);
    if (removed > 0)
    {
        QCAI_DEBUG("Tools", QStringLiteral("Unregistered tool: %1").arg(name));
    }
    return removed > 0;
}

/**
 * Returns all registered tools.
 */
QList<std::shared_ptr<i_tool_t>> tool_registry_t::all_tools() const
{
    return this->tools.values();
}

/**
 * Serializes registered tool metadata for the agent prompt.
 */
QJsonArray tool_registry_t::tool_descriptions_json() const
{
    QJsonArray arr;
    for (auto it = this->tools.begin(); ((it != this->tools.end()) == true); ++it)
    {
        QJsonObject desc;
        desc["name"] = it.value()->name();
        desc["description"] = it.value()->description();
        desc["args"] = it.value()->args_schema();
        arr.append(desc);
    }
    return arr;
}

}  // namespace qcai2
