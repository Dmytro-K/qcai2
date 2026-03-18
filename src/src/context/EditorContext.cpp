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

ProjectExplorer::Project *resolve_project(const QString &selected_project_file_path)
{
    if (selected_project_file_path.isEmpty() == false)
    {
        if (auto *project = ProjectExplorer::ProjectManager::projectWithProjectFilePath(
                Utils::FilePath::fromString(selected_project_file_path));
            ((project != nullptr) == true))
        {
            return project;
        }
    }

    return ProjectExplorer::ProjectManager::startupProject();
}

bool belongs_to_project(const Utils::FilePath &file_path, ProjectExplorer::Project *project)
{
    if ((((project == nullptr) || file_path.isEmpty()) == true))
    {
        return project == nullptr;
    }

    return ProjectExplorer::ProjectManager::projectsForFile(file_path).contains(project);
}

}  // namespace

editor_context_t::editor_context_t(QObject *parent) : QObject(parent)
{
}

editor_context_t::snapshot_t editor_context_t::capture() const
{
    snapshot_t s;
    ProjectExplorer::Project *project = resolve_project(this->active_project_file_path);

    // Current editor file & selection
    if (Core::IEditor *editor = Core::EditorManager::currentEditor();
        ((editor != nullptr) == true))
    {
        if (Core::IDocument *doc = editor->document(); ((doc != nullptr) == true))
        {
            if (((belongs_to_project(doc->filePath(), project)) == true))
            {
                s.file_path = doc->filePath().toUrlishString();
            }
        }

        if (((!s.file_path.isEmpty()) == true))
        {
            if (auto *text_editor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
                ((text_editor != nullptr) == true))
            {
                QTextCursor tc = text_editor->textCursor();
                s.cursor_line = tc.blockNumber() + 1;
                s.cursor_column = tc.columnNumber() + 1;
                s.selected_text = tc.selectedText();
            }
        }
    }

    // Open documents
    const auto docs = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : docs)
    {
        if (belongs_to_project(doc->filePath(), project))
        {
            s.open_files.append(doc->filePath().toUrlishString());
        }
    }

    // Project & build directories
    if (((project != nullptr) == true))
    {
        s.project_name = project->displayName();
        s.project_dir = project->projectDirectory().toUrlishString();
        s.project_file_path = project->projectFilePath().toUrlishString();

        if (auto *target = project->activeTarget(); ((target != nullptr) == true))
        {
            s.target_name = target->displayName();

            if (auto *bc = target->activeBuildConfiguration(); ((bc != nullptr) == true))
            {
                s.build_dir = bc->buildDirectory().toUrlishString();
                s.build_type = ProjectExplorer::BuildConfiguration::buildTypeName(bc->buildType());
            }

            if (auto *kit = target->kit(); ((kit != nullptr) == true))
            {
                s.kit_name = kit->displayName();
                if (auto *tc = ProjectExplorer::ToolchainKitAspect::cxxToolchain(kit);
                    ((tc != nullptr) == true))
                {
                    s.compiler_name = tc->displayName();
                    s.compiler_path = tc->compilerCommand().toUrlishString();
                }
            }
        }
    }

    QCAI_DEBUG("Context",
               QStringLiteral("Captured: file=%1 line=%2 project=%3 open_files=%4")
                   .arg(s.file_path.isEmpty() ? QStringLiteral("(none)") : s.file_path)
                   .arg(s.cursor_line)
                   .arg(s.project_dir.isEmpty() ? QStringLiteral("(none)") : s.project_dir)
                   .arg(s.open_files.size()));

    return s;
}

QList<editor_context_t::project_info_t> editor_context_t::open_projects() const
{
    QList<project_info_t> projects;
    const auto open_projects = ProjectExplorer::ProjectManager::projects();
    projects.reserve(open_projects.size());

    for (auto *project : open_projects)
    {
        if (project == nullptr)
        {
            continue;
        }

        project_info_t info;
        info.project_name = project->displayName();
        info.project_dir = project->projectDirectory().toUrlishString();
        info.project_file_path = project->projectFilePath().toUrlishString();
        projects.append(info);
    }

    return projects;
}

void editor_context_t::set_selected_project_file_path(const QString &project_file_path)
{
    this->active_project_file_path = project_file_path;
}

QString editor_context_t::selected_project_file_path() const
{
    return this->active_project_file_path;
}

QString editor_context_t::to_prompt_fragment() const
{
    snapshot_t s = this->capture();
    QString f;
    f += QStringLiteral("Active file: %1\n").arg(s.file_path.isEmpty() ? "(none)" : s.file_path);
    if (((s.cursor_line > 0) == true))
    {
        f += QStringLiteral("Cursor: line %1, col %2\n").arg(s.cursor_line).arg(s.cursor_column);
    }
    if (((!s.selected_text.isEmpty()) == true))
    {
        f += QStringLiteral("Selected text: \"%1\"\n").arg(s.selected_text.left(500));
    }

    if (((!s.project_name.isEmpty()) == true))
    {
        f += QStringLiteral("Project: %1\n").arg(s.project_name);
    }
    f += QStringLiteral("Project dir: %1\n")
             .arg(s.project_dir.isEmpty() ? "(none)" : s.project_dir);
    if (((!s.project_file_path.isEmpty()) == true))
    {
        f += QStringLiteral("Project file: %1\n").arg(s.project_file_path);
    }
    f += QStringLiteral("Build dir: %1\n").arg(s.build_dir.isEmpty() ? "(none)" : s.build_dir);
    if (((!s.build_type.isEmpty()) == true))
    {
        f += QStringLiteral("Build type: %1\n").arg(s.build_type);
    }
    if (((!s.kit_name.isEmpty()) == true))
    {
        f += QStringLiteral("Kit: %1\n").arg(s.kit_name);
    }
    if (((!s.compiler_name.isEmpty()) == true))
    {
        f += QStringLiteral("Compiler: %1 (%2)\n").arg(s.compiler_name, s.compiler_path);
    }
    if (((!s.target_name.isEmpty()) == true))
    {
        f += QStringLiteral("Target: %1\n").arg(s.target_name);
    }
    if (((!s.open_files.isEmpty()) == true))
    {
        f += QStringLiteral("Open files: %1\n").arg(s.open_files.join(", "));
    }
    return f;
}

QString editor_context_t::file_contents_fragment(int max_chars) const
{
    QString result;
    int remaining = max_chars;
    ProjectExplorer::Project *project = resolve_project(this->active_project_file_path);

    // Collect documents: active file first, then other open tabs
    QList<Core::IDocument *> ordered;
    Core::IDocument *active_doc = nullptr;
    if (Core::IEditor *editor = Core::EditorManager::currentEditor();
        ((editor != nullptr) == true))
    {
        Core::IDocument *candidate = editor->document();
        if ((((candidate != nullptr) && belongs_to_project(candidate->filePath(), project)) ==
             true))
        {
            active_doc = candidate;
        }
    }

    if (((active_doc != nullptr) == true))
    {
        ordered.append(active_doc);
    }

    const auto docs = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : docs)
    {
        if (doc != active_doc && belongs_to_project(doc->filePath(), project))
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
                                  .arg(max_chars - remaining));
    }

    return result;
}

}  // namespace qcai2
