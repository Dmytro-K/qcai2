#include "project_root_resolver.h"

#include <extensionsystem/pluginmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <utils/filepath.h>

#include <QFileInfo>

namespace qcai2
{

QString project_root_for_file_path(const QString &file_path)
{
    if (file_path.trimmed().isEmpty())
    {
        return {};
    }

    const QFileInfo file_info(file_path);
    if (file_info.isAbsolute() == false)
    {
        return {};
    }

    if (ExtensionSystem::PluginManager::instance() == nullptr ||
        ProjectExplorer::ProjectManager::instance() == nullptr)
    {
        return {};
    }

    const auto projects =
        ProjectExplorer::ProjectManager::projectsForFile(Utils::FilePath::fromString(file_path));
    if (!projects.isEmpty() && projects.first() != nullptr)
    {
        return projects.first()->projectDirectory().toUrlishString();
    }

    return {};
}

}  // namespace qcai2
