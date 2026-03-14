/*! Bridges configured MCP servers into the agent tool registry at runtime. */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace qcai2
{

class ToolRegistry;

/**
 * Loads configured MCP servers, initializes stdio sessions, and registers their tools.
 */
class McpToolManager final : public QObject
{
public:
    explicit McpToolManager(ToolRegistry *toolRegistry, QObject *parent = nullptr);
    ~McpToolManager() override;

    /**
     * Rebuilds the dynamic MCP tool set for the current project context.
     * @param projectDir Active project directory, or empty for global-only tools.
     * @return User-visible status lines describing what was loaded or skipped.
     */
    QStringList refreshForProject(const QString &projectDir);

    /**
     * Calls one registered MCP tool synchronously.
     * @param exposedToolName Dynamic tool name visible to the agent.
     * @param args JSON arguments supplied by the agent.
     * @return Flattened tool result or an explicit error string.
     */
    QString executeTool(const QString &exposedToolName, const QJsonObject &args) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace qcai2
