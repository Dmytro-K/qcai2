/*! @file
    @brief Implements command, tool, and workspace safety checks.
*/

#include "SafetyPolicy.h"
#include "../util/Logger.h"

#include <QDir>
#include <QFileInfo>

namespace qcai2
{

safety_policy_t::safety_policy_t(QObject *parent) : QObject(parent)
{
    // Default allowlisted commands (safe for automatic execution)
    this->allowed_commands = {
        QStringLiteral("cmake"), QStringLiteral("ninja"), QStringLiteral("make"),
        QStringLiteral("ctest"), QStringLiteral("rg"),    QStringLiteral("git"),
        QStringLiteral("patch"),
    };

    // Tools that always require user approval
    this->approval_required_tools = {
        QStringLiteral("apply_patch"),
    };
}

bool safety_policy_t::is_command_allowed(const QString &program) const
{
    const QString base = QFileInfo(program).baseName();
    bool allowed =
        this->allowed_commands.contains(base) || this->allowed_commands.contains(program);
    QCAI_DEBUG("Safety",
               QStringLiteral("Command '%1' allowed: %2")
                   .arg(program, allowed ? QStringLiteral("yes") : QStringLiteral("no")));
    return allowed;
}

QString safety_policy_t::requires_approval(const QString &toolName, int files_changed,
                                           int lines_changed) const
{
    // Always require approval for certain tools
    if (this->approval_required_tools.contains(toolName) == true)
    {
        return QStringLiteral("Tool '%1' modifies files and requires approval.").arg(toolName);
    }

    // Check size limits
    if (((files_changed > this->max_changed_files_limit) == true))
    {
        return QStringLiteral("Too many files changed (%1 > %2).")
            .arg(files_changed)
            .arg(this->max_changed_files_limit);
    }

    if (((lines_changed > this->max_diff_lines_limit) == true))
    {
        return QStringLiteral("Too many lines changed (%1 > %2).")
            .arg(lines_changed)
            .arg(this->max_diff_lines_limit);
    }

    return {};  // No approval needed
}

bool safety_policy_t::is_path_safe(const QString &path, const QString &work_dir) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString workCanonical = QDir(work_dir).canonicalPath();

    if (canonical.isEmpty() == true)
    {
        QCAI_WARN("Safety", QStringLiteral("Path '%1' does not exist").arg(path));
        return false;
    }

    bool safe = canonical.startsWith(workCanonical);
    QCAI_DEBUG("Safety",
               QStringLiteral("Path safe check: '%1' in '%2' → %3")
                   .arg(path, work_dir, safe ? QStringLiteral("yes") : QStringLiteral("no")));
    return safe;
}

}  // namespace qcai2
