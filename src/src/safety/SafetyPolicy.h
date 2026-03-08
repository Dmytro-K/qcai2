#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace qcai2
{

/**
 * Applies command, tool, and workspace safety checks for agent actions.
 */
class SafetyPolicy : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a safety policy with default allowlists and limits.
     * @param parent Owning QObject.
     */
    explicit SafetyPolicy(QObject *parent = nullptr);

    /**
     * Checks whether a command may run without extra approval.
     * @param program Command name or path.
     * @return True when the command is on the allowlist.
     */
    bool isCommandAllowed(const QString &program) const;

    /**
     * Checks whether a tool invocation requires approval.
     * @param toolName Tool being invoked.
     * @param filesChanged Number of files modified by the action.
     * @param linesChanged Number of lines modified by the action.
     * @return Empty string when allowed, otherwise the approval reason.
     */
    QString requiresApproval(const QString &toolName, int filesChanged = 0,
                             int linesChanged = 0) const;

    /**
     * Sets the maximum allowed agent iterations.
     * @param n Maximum iteration count.
     */
    void setMaxIterations(int n)
    {
        m_maxIterations = n;
    }

    /**
     * Sets the maximum allowed tool calls.
     * @param n Maximum tool call count.
     */
    void setMaxToolCalls(int n)
    {
        m_maxToolCalls = n;
    }

    /**
     * Sets the maximum changed lines allowed without approval.
     * @param n Maximum changed line count.
     */
    void setMaxDiffLines(int n)
    {
        m_maxDiffLines = n;
    }

    /**
     * Sets the maximum changed files allowed without approval.
     * @param n Maximum changed file count.
     */
    void setMaxChangedFiles(int n)
    {
        m_maxChangedFiles = n;
    }

    /**
     * Returns the maximum allowed agent iterations.
     * @return Iteration limit.
     */
    int maxIterations() const
    {
        return m_maxIterations;
    }

    /**
     * Returns the maximum allowed tool calls.
     * @return Tool call limit.
     */
    int maxToolCalls() const
    {
        return m_maxToolCalls;
    }

    /**
     * Returns the maximum changed lines allowed without approval.
     * @return Changed line limit.
     */
    int maxDiffLines() const
    {
        return m_maxDiffLines;
    }

    /**
     * Returns the maximum changed files allowed without approval.
     * @return Changed file limit.
     */
    int maxChangedFiles() const
    {
        return m_maxChangedFiles;
    }

    /**
     * Checks whether a path resolves inside the allowed workspace.
     * @param path Path to validate.
     * @param workDir Workspace root directory.
     * @return True when the canonical path stays inside the workspace.
     */
    bool isPathSafe(const QString &path, const QString &workDir) const;

private:
    /** Commands allowed to run automatically. */
    QSet<QString> m_allowedCommands;

    /** Tools that always require user approval. */
    QSet<QString> m_approvalRequiredTools;

    /** Maximum agent loop iterations. */
    int m_maxIterations = 8;

    /** Maximum number of tool calls per request. */
    int m_maxToolCalls = 25;

    /** Maximum changed lines allowed without approval. */
    int m_maxDiffLines = 300;

    /** Maximum changed files allowed without approval. */
    int m_maxChangedFiles = 10;

};

}  // namespace qcai2
