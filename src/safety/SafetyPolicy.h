#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace qcai2
{

// Enforces safety rules: allowlisted commands, approval requirements, limits.
class SafetyPolicy : public QObject
{
    Q_OBJECT
public:
    explicit SafetyPolicy(QObject *parent = nullptr);

    // Check if a command/program is allowlisted for automatic execution.
    bool isCommandAllowed(const QString &program) const;

    // Check if a tool invocation requires user approval.
    // Returns an empty string if OK, or a reason string if approval is needed.
    QString requiresApproval(const QString &toolName, int filesChanged = 0,
                             int linesChanged = 0) const;

    // Configurable limits
    void setMaxIterations(int n)
    {
        m_maxIterations = n;
    }
    void setMaxToolCalls(int n)
    {
        m_maxToolCalls = n;
    }
    void setMaxDiffLines(int n)
    {
        m_maxDiffLines = n;
    }
    void setMaxChangedFiles(int n)
    {
        m_maxChangedFiles = n;
    }

    int maxIterations() const
    {
        return m_maxIterations;
    }
    int maxToolCalls() const
    {
        return m_maxToolCalls;
    }
    int maxDiffLines() const
    {
        return m_maxDiffLines;
    }
    int maxChangedFiles() const
    {
        return m_maxChangedFiles;
    }

    // Check if a file path is within the allowed workspace
    bool isPathSafe(const QString &path, const QString &workDir) const;

private:
    QSet<QString> m_allowedCommands;
    QSet<QString> m_approvalRequiredTools;

    int m_maxIterations = 8;
    int m_maxToolCalls = 25;
    int m_maxDiffLines = 300;
    int m_maxChangedFiles = 10;
};

}  // namespace qcai2
