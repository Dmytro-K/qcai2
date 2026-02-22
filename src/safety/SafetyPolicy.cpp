#include "SafetyPolicy.h"
#include "../util/Logger.h"

#include <QDir>
#include <QFileInfo>

namespace Qcai2 {

SafetyPolicy::SafetyPolicy(QObject *parent) : QObject(parent)
{
    // Default allowlisted commands (safe for automatic execution)
    m_allowedCommands = {
        QStringLiteral("cmake"),
        QStringLiteral("ninja"),
        QStringLiteral("make"),
        QStringLiteral("ctest"),
        QStringLiteral("rg"),
        QStringLiteral("git"),
        QStringLiteral("patch"),
    };

    // Tools that always require user approval
    m_approvalRequiredTools = {
        QStringLiteral("apply_patch"),
    };
}

bool SafetyPolicy::isCommandAllowed(const QString &program) const
{
    const QString base = QFileInfo(program).baseName();
    bool allowed = m_allowedCommands.contains(base) || m_allowedCommands.contains(program);
    QCAI_DEBUG("Safety", QStringLiteral("Command '%1' allowed: %2")
        .arg(program, allowed ? QStringLiteral("yes") : QStringLiteral("no")));
    return allowed;
}

QString SafetyPolicy::requiresApproval(const QString &toolName,
                                        int filesChanged,
                                        int linesChanged) const
{
    // Always require approval for certain tools
    if (m_approvalRequiredTools.contains(toolName))
        return QStringLiteral("Tool '%1' modifies files and requires approval.").arg(toolName);

    // Check size limits
    if (filesChanged > m_maxChangedFiles)
        return QStringLiteral("Too many files changed (%1 > %2).")
            .arg(filesChanged).arg(m_maxChangedFiles);

    if (linesChanged > m_maxDiffLines)
        return QStringLiteral("Too many lines changed (%1 > %2).")
            .arg(linesChanged).arg(m_maxDiffLines);

    return {}; // No approval needed
}

bool SafetyPolicy::isPathSafe(const QString &path, const QString &workDir) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString workCanonical = QDir(workDir).canonicalPath();

    if (canonical.isEmpty()) {
        QCAI_WARN("Safety", QStringLiteral("Path '%1' does not exist").arg(path));
        return false;
    }

    bool safe = canonical.startsWith(workCanonical);
    QCAI_DEBUG("Safety", QStringLiteral("Path safe check: '%1' in '%2' → %3")
        .arg(path, workDir, safe ? QStringLiteral("yes") : QStringLiteral("no")));
    return safe;
}

} // namespace Qcai2
