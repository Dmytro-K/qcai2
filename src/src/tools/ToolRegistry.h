#pragma once

#include "ITool.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <memory>

namespace qcai2
{

/**
 * Central registry that owns and exposes agent tools.
 */
class tool_registry_t : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates an empty registry.
     * @param parent Parent QObject that owns this instance.
     */
    explicit tool_registry_t(QObject *parent = nullptr);

    /**
     * Registers or replaces a tool by its name.
     * @param tool Tool instance to register.
     */
    void register_tool(const std::shared_ptr<i_tool_t> &tool);

    /**
     * Looks up a tool by name.
     * @param name Name value.
     */
    i_tool_t *tool(const QString &name) const;

    /**
     * Removes a previously registered tool by name.
     * @param name Stable tool name to remove.
     * @return True when a tool was removed.
     */
    bool unregister_tool(const QString &name);

    /**
     * Returns all registered tools in map order.
     */
    QList<std::shared_ptr<i_tool_t>> all_tools() const;

    /**
     * Builds the JSON description array injected into the agent prompt.
     */
    QJsonArray tool_descriptions_json() const;

private:
    /** Registered tools keyed by stable tool name. */
    QMap<QString, std::shared_ptr<i_tool_t>> tools;
};

}  // namespace qcai2
