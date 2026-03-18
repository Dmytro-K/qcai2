/*! Bridges configured MCP servers into the agent tool registry at runtime. */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace qcai2
{

class tool_registry_t;

/**
 * Loads configured MCP servers, initializes stdio sessions, and registers their tools.
 */
class mcp_tool_manager_t final : public QObject
{
public:
    explicit mcp_tool_manager_t(tool_registry_t *tool_registry, QObject *parent = nullptr);
    ~mcp_tool_manager_t() override;

    /**
     * Rebuilds the dynamic MCP tool set for the current project context.
     * @param projectDir Active project directory, or empty for global-only tools.
     * @return User-visible status lines describing what was loaded or skipped.
     */
    QStringList refresh_for_project(const QString &project_dir);

    /**
     * Calls one registered MCP tool synchronously.
     * @param exposedToolName Dynamic tool name visible to the agent.
     * @param args JSON arguments supplied by the agent.
     * @return Flattened tool result or an explicit error string.
     */
    QString execute_tool(const QString &exposed_tool_name, const QJsonObject &args) const;

private:
    class impl_t;
    std::unique_ptr<impl_t> impl;
};

}  // namespace qcai2
