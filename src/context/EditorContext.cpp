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

#include <QTextCursor>

namespace qcai2
{

EditorContext::EditorContext(QObject *parent) : QObject(parent)
{
}

EditorContext::Snapshot EditorContext::capture() const
{
    Snapshot s;

    // Current editor file & selection
    if (Core::IEditor *editor = Core::EditorManager::currentEditor())
    {
        if (Core::IDocument *doc = editor->document())
            s.filePath = doc->filePath().toUrlishString();

        if (auto *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor))
        {
            QTextCursor tc = textEditor->textCursor();
            s.cursorLine = tc.blockNumber() + 1;
            s.cursorColumn = tc.columnNumber() + 1;
            s.selectedText = tc.selectedText();
        }
    }

    // Open documents
    const auto docs = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : docs)
        s.openFiles.append(doc->filePath().toUrlishString());

    // Project & build directories
    if (auto *project = ProjectExplorer::ProjectManager::startupProject())
    {
        s.projectName = project->displayName();
        s.projectDir = project->projectDirectory().toUrlishString();
        s.projectFilePath = project->projectFilePath().toUrlishString();

        if (auto *target = project->activeTarget())
        {
            s.targetName = target->displayName();

            if (auto *bc = target->activeBuildConfiguration())
            {
                s.buildDir = bc->buildDirectory().toUrlishString();
                s.buildType = ProjectExplorer::BuildConfiguration::buildTypeName(bc->buildType());
            }

            if (auto *kit = target->kit())
            {
                s.kitName = kit->displayName();
                if (auto *tc = ProjectExplorer::ToolchainKitAspect::cxxToolchain(kit))
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

QString EditorContext::toPromptFragment() const
{
    Snapshot s = capture();
    QString f;
    f += QStringLiteral("Active file: %1\n").arg(s.filePath.isEmpty() ? "(none)" : s.filePath);
    if (s.cursorLine > 0)
        f += QStringLiteral("Cursor: line %1, col %2\n").arg(s.cursorLine).arg(s.cursorColumn);
    if (!s.selectedText.isEmpty())
        f += QStringLiteral("Selected text: \"%1\"\n").arg(s.selectedText.left(500));

    if (!s.projectName.isEmpty())
        f += QStringLiteral("Project: %1\n").arg(s.projectName);
    f += QStringLiteral("Project dir: %1\n").arg(s.projectDir.isEmpty() ? "(none)" : s.projectDir);
    if (!s.projectFilePath.isEmpty())
        f += QStringLiteral("Project file: %1\n").arg(s.projectFilePath);
    f += QStringLiteral("Build dir: %1\n").arg(s.buildDir.isEmpty() ? "(none)" : s.buildDir);
    if (!s.buildType.isEmpty())
        f += QStringLiteral("Build type: %1\n").arg(s.buildType);
    if (!s.kitName.isEmpty())
        f += QStringLiteral("Kit: %1\n").arg(s.kitName);
    if (!s.compilerName.isEmpty())
        f += QStringLiteral("Compiler: %1 (%2)\n").arg(s.compilerName, s.compilerPath);
    if (!s.targetName.isEmpty())
        f += QStringLiteral("Target: %1\n").arg(s.targetName);
    if (!s.openFiles.isEmpty())
        f += QStringLiteral("Open files: %1\n").arg(s.openFiles.join(", "));
    return f;
}

QString EditorContext::fileContentsFragment(int maxChars) const
{
    QString result;
    int remaining = maxChars;

    // Collect documents: active file first, then other open tabs
    QList<Core::IDocument *> ordered;
    Core::IDocument *activeDoc = nullptr;
    if (Core::IEditor *editor = Core::EditorManager::currentEditor())
        activeDoc = editor->document();

    if (activeDoc)
        ordered.append(activeDoc);

    const auto docs = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : docs)
    {
        if (doc != activeDoc)
            ordered.append(doc);
    }

    for (Core::IDocument *doc : ordered)
    {
        if (remaining <= 0)
            break;

        const QString path = doc->filePath().toUrlishString();
        if (path.isEmpty())
            continue;

        // Read content from the editor buffer (in-memory, no disk I/O)
        QString content;
        const auto editors = Core::DocumentModel::editorsForDocument(doc);
        if (!editors.isEmpty())
        {
            if (auto *te = qobject_cast<TextEditor::BaseTextEditor *>(editors.first()))
            {
                if (auto *tw = te->editorWidget())
                    content = tw->toPlainText();
            }
        }

        if (content.isEmpty())
            continue;

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
