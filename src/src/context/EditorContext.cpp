/*! Implements prompt context capture from the active editor and selected project. */
#include "EditorContext.h"
#include "../util/Logger.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/idocument.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainkitaspect.h>
#include <texteditor/texteditor.h>
#include <utils/filepath.h>

#include <QTextCursor>

namespace qcai2
{

namespace
{

ProjectExplorer::Project *resolveProject(const QString &selectedProjectFilePath)
{
    if (selectedProjectFilePath.isEmpty() == false)
    {
        if (auto *project = ProjectExplorer::ProjectManager::projectWithProjectFilePath(
                Utils::FilePath::fromString(selectedProjectFilePath));
            ((project != nullptr) == true))
        {
            return project;
        }
    }

    return ProjectExplorer::ProjectManager::startupProject();
}

bool belongsToProject(const Utils::FilePath &filePath, ProjectExplorer::Project *project)
{
    if ((((project == nullptr) || filePath.isEmpty()) == true))
    {
        return project == nullptr;
    }

    return ProjectExplorer::ProjectManager::projectsForFile(filePath).contains(project);
}

}  // namespace

EditorContext::EditorContext(QObject *parent) : QObject(parent)
{
}

EditorContext::Snapshot EditorContext::capture() const
{
    Snapshot s;
    ProjectExplorer::Project *project = resolveProject(m_selectedProjectFilePath);

    // Current editor file & selection
    if (Core::IEditor *editor = Core::EditorManager::currentEditor();
        ((editor != nullptr) == true))
    {
        if (Core::IDocument *doc = editor->document(); ((doc != nullptr) == true))
        {
            if (((belongsToProject(doc->filePath(), project)) == true))
            {
                s.filePath = doc->filePath().toUrlishString();
            }
        }

        if (((!s.filePath.isEmpty()) == true))
        {
            if (auto *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
                ((textEditor != nullptr) == true))
            {
                QTextCursor tc = textEditor->textCursor();
                s.cursorLine = tc.blockNumber() + 1;
                s.cursorColumn = tc.columnNumber() + 1;
                s.selectedText = tc.selectedText();
            }
        }
    }

    // Open documents
    const auto docs = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : docs)
    {
        if (belongsToProject(doc->filePath(), project))
        {
            s.openFiles.append(doc->filePath().toUrlishString());
        }
    }

    // Project & build directories
    if (((project != nullptr) == true))
    {
        s.projectName = project->displayName();
        s.projectDir = project->projectDirectory().toUrlishString();
        s.projectFilePath = project->projectFilePath().toUrlishString();

        if (auto *target = project->activeTarget(); ((target != nullptr) == true))
        {
            s.targetName = target->displayName();

            if (auto *bc = target->activeBuildConfiguration(); ((bc != nullptr) == true))
            {
                s.buildDir = bc->buildDirectory().toUrlishString();
                s.buildType = ProjectExplorer::BuildConfiguration::buildTypeName(bc->buildType());
            }

            if (auto *kit = target->kit(); ((kit != nullptr) == true))
            {
                s.kitName = kit->displayName();
                if (auto *tc = ProjectExplorer::ToolchainKitAspect::cxxToolchain(kit);
                    ((tc != nullptr) == true))
                {
                    s.compilerName = tc->displayName();
                    s.compilerPath = tc->compilerCommand().toUrlishString();
                }
            }
        }
    }

    QCAI_DEBUG("Context",
               QStringLiteral("Captured: file=%1 line=%2 project=%3 openFiles=%4")
                   .arg(s.filePath.isEmpty() ? QStringLiteral("(none)") : s.filePath)
                   .arg(s.cursorLine)
                   .arg(s.projectDir.isEmpty() ? QStringLiteral("(none)") : s.projectDir)
                   .arg(s.openFiles.size()));

    return s;
}

QList<EditorContext::ProjectInfo> EditorContext::openProjects() const
{
    QList<ProjectInfo> projects;
    const auto openProjects = ProjectExplorer::ProjectManager::projects();
    projects.reserve(openProjects.size());

    for (auto *project : openProjects)
    {
        if (project == nullptr)
        {
            continue;
        }

        ProjectInfo info;
        info.projectName = project->displayName();
        info.projectDir = project->projectDirectory().toUrlishString();
        info.projectFilePath = project->projectFilePath().toUrlishString();
        projects.append(info);
    }

    return projects;
}

void EditorContext::setSelectedProjectFilePath(const QString &projectFilePath)
{
    m_selectedProjectFilePath = projectFilePath;
}

QString EditorContext::selectedProjectFilePath() const
{
    return m_selectedProjectFilePath;
}

QString EditorContext::toPromptFragment() const
{
    Snapshot s = capture();
    QString f;
    f += QStringLiteral("Active file: %1\n").arg(s.filePath.isEmpty() ? "(none)" : s.filePath);
    if (((s.cursorLine > 0) == true))
    {
        f += QStringLiteral("Cursor: line %1, col %2\n").arg(s.cursorLine).arg(s.cursorColumn);
    }
    if (((!s.selectedText.isEmpty()) == true))
    {
        f += QStringLiteral("Selected text: \"%1\"\n").arg(s.selectedText.left(500));
    }

    if (((!s.projectName.isEmpty()) == true))
    {
        f += QStringLiteral("Project: %1\n").arg(s.projectName);
    }
    f += QStringLiteral("Project dir: %1\n").arg(s.projectDir.isEmpty() ? "(none)" : s.projectDir);
    if (((!s.projectFilePath.isEmpty()) == true))
    {
        f += QStringLiteral("Project file: %1\n").arg(s.projectFilePath);
    }
    f += QStringLiteral("Build dir: %1\n").arg(s.buildDir.isEmpty() ? "(none)" : s.buildDir);
    if (((!s.buildType.isEmpty()) == true))
    {
        f += QStringLiteral("Build type: %1\n").arg(s.buildType);
    }
    if (((!s.kitName.isEmpty()) == true))
    {
        f += QStringLiteral("Kit: %1\n").arg(s.kitName);
    }
    if (((!s.compilerName.isEmpty()) == true))
    {
        f += QStringLiteral("Compiler: %1 (%2)\n").arg(s.compilerName, s.compilerPath);
    }
    if (((!s.targetName.isEmpty()) == true))
    {
        f += QStringLiteral("Target: %1\n").arg(s.targetName);
    }
    if (((!s.openFiles.isEmpty()) == true))
    {
        f += QStringLiteral("Open files: %1\n").arg(s.openFiles.join(", "));
    }
    return f;
}

QString EditorContext::fileContentsFragment(int maxChars) const
{
    QString result;
    int remaining = maxChars;
    ProjectExplorer::Project *project = resolveProject(m_selectedProjectFilePath);

    // Collect documents: active file first, then other open tabs
    QList<Core::IDocument *> ordered;
    Core::IDocument *activeDoc = nullptr;
    if (Core::IEditor *editor = Core::EditorManager::currentEditor();
        ((editor != nullptr) == true))
    {
        Core::IDocument *candidate = editor->document();
        if ((((candidate != nullptr) && belongsToProject(candidate->filePath(), project)) == true))
        {
            activeDoc = candidate;
        }
    }

    if (((activeDoc != nullptr) == true))
    {
        ordered.append(activeDoc);
    }

    const auto docs = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : docs)
    {
        if (doc != activeDoc && belongsToProject(doc->filePath(), project))
        {
            ordered.append(doc);
        }
    }

    for (Core::IDocument *doc : ordered)
    {
        if (remaining <= 0)
        {
            break;
        }

        const QString path = doc->filePath().toUrlishString();
        if (path.isEmpty())
        {
            continue;
        }

        // Read content from the editor buffer (in-memory, no disk I/O)
        QString content;
        const auto editors = Core::DocumentModel::editorsForDocument(doc);
        if (!editors.isEmpty())
        {
            if (auto *te = qobject_cast<TextEditor::BaseTextEditor *>(editors.first()))
            {
                if (auto *tw = te->editorWidget())
                {
                    content = tw->toPlainText();
                }
            }
        }

        if (content.isEmpty())
        {
            continue;
        }

        // Truncate if needed
        if (content.length() > remaining)
        {
            content = content.left(remaining);
            content += QStringLiteral("\n... (truncated)");
        }

        result += QStringLiteral("── %1 ──\n%2\n\n").arg(path, content);
        remaining -= static_cast<int>(content.length());
    }

    if (!result.isEmpty())
    {
        QCAI_DEBUG("Context", QStringLiteral("File contents: %1 files, %2 chars")
                                  .arg(ordered.size())
                                  .arg(maxChars - remaining));
    }

    return result;
}

}  // namespace qcai2
