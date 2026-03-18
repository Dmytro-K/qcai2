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
class safety_policy_t : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a safety policy with default allowlists and limits.
     * @param parent Owning QObject.
     */
    explicit safety_policy_t(QObject *parent = nullptr);

    /**
     * Checks whether a command may run without extra approval.
     * @param program Command name or path.
     * @return True when the command is on the allowlist.
     */
    bool is_command_allowed(const QString &program) const;

    /**
     * Checks whether a tool invocation requires approval.
     * @param toolName Tool being invoked.
     * @param filesChanged Number of files modified by the action.
     * @param linesChanged Number of lines modified by the action.
     * @return Empty string when allowed, otherwise the approval reason.
     */
    QString requires_approval(const QString &tool_name, int files_changed = 0,
                              int lines_changed = 0) const;

    /**
     * Sets the maximum allowed agent iterations.
     * @param n Maximum iteration count.
     */
    void set_max_iterations(int n)
    {
        this->max_iterations_limit = n;
    }

    /**
     * Sets the maximum allowed tool calls.
     * @param n Maximum tool call count.
     */
    void set_max_tool_calls(int n)
    {
        this->max_tool_calls_limit = n;
    }

    /**
     * Sets the maximum changed lines allowed without approval.
     * @param n Maximum changed line count.
     */
    void set_max_diff_lines(int n)
    {
        this->max_diff_lines_limit = n;
    }

    /**
     * Sets the maximum changed files allowed without approval.
     * @param n Maximum changed file count.
     */
    void set_max_changed_files(int n)
    {
        this->max_changed_files_limit = n;
    }

    /**
     * Returns the maximum allowed agent iterations.
     * @return Iteration limit.
     */
    int max_iterations() const
    {
        return this->max_iterations_limit;
    }

    /**
     * Returns the maximum allowed tool calls.
     * @return Tool call limit.
     */
    int max_tool_calls() const
    {
        return this->max_tool_calls_limit;
    }

    /**
     * Returns the maximum changed lines allowed without approval.
     * @return Changed line limit.
     */
    int max_diff_lines() const
    {
        return this->max_diff_lines_limit;
    }

    /**
     * Returns the maximum changed files allowed without approval.
     * @return Changed file limit.
     */
    int max_changed_files() const
    {
        return this->max_changed_files_limit;
    }

    /**
     * Checks whether a path resolves inside the allowed workspace.
     * @param path Path to validate.
     * @param workDir Workspace root directory.
     * @return True when the canonical path stays inside the workspace.
     */
    bool is_path_safe(const QString &path, const QString &work_dir) const;

private:
    /** Commands allowed to run automatically. */
    QSet<QString> allowed_commands;

    /** Tools that always require user approval. */
    QSet<QString> approval_required_tools;

    /** Maximum agent loop iterations. */
    int max_iterations_limit = 8;

    /** Maximum number of tool calls per request. */
    int max_tool_calls_limit = 25;

    /** Maximum changed lines allowed without approval. */
    int max_diff_lines_limit = 300;

    /** Maximum changed files allowed without approval. */
    int max_changed_files_limit = 10;
};

}  // namespace qcai2
