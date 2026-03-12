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
class ToolRegistry : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates an empty registry.
     * @param parent Parent QObject that owns this instance.
     */
    explicit ToolRegistry(QObject *parent = nullptr);

    /**
     * Registers or replaces a tool by its name.
     * @param tool Tool instance to register.
     */
    void registerTool(const std::shared_ptr<ITool>& tool);

    /**
     * Looks up a tool by name.
     * @param name Name value.
     */
    ITool *tool(const QString &name) const;

    /**
     * Returns all registered tools in map order.
     */
    QList<std::shared_ptr<ITool>> allTools() const;

    /**
     * Builds the JSON description array injected into the agent prompt.
     */
    QJsonArray toolDescriptionsJson() const;

private:
    /** Registered tools keyed by stable tool name. */
    QMap<QString, std::shared_ptr<ITool>> m_tools;

};

}  // namespace qcai2
