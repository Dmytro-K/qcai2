/*! Declares helpers that capture active editor and project state for prompts. */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace qcai2
{

/**
 * Captures active editor, project, and build information for prompt generation.
 */
class EditorContext : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a context provider bound to the current Qt Creator session.
     * @param parent Parent QObject that owns this instance.
     */
    explicit EditorContext(QObject *parent = nullptr);

    /**
     * Snapshot of the current editor, project, and build state.
     */
    struct Snapshot
    {
        /**
         * Active document path, or empty when no editor is focused.
         */
        QString filePath;

        /**
         * Current text selection from the active editor.
         */
        QString selectedText;

        /**
         * 1-based cursor line in the active editor.
         */
        int cursorLine = -1;

        /**
         * 1-based cursor column in the active editor.
         */
        int cursorColumn = -1;

        /**
         * Paths for all open documents.
         */
        QStringList openFiles;

        /**
         * Startup project display name.
         */
        QString projectName;

        /**
         * Startup project directory.
         */
        QString projectDir;

        /**
         * Project file path reported by Qt Creator.
         */
        QString projectFilePath;

        /**
         * Active build directory, when available.
         */
        QString buildDir;

        /**
         * Active build type such as Debug or Release.
         */
        QString buildType;

        /**
         * Active kit name.
         */
        QString kitName;

        /**
         * Active C++ compiler display name.
         */
        QString compilerName;

        /**
         * Active C++ compiler executable path.
         */
        QString compilerPath;

        /**
         * Active target display name.
         */
        QString targetName;

    };

    /**
     * Returns a fresh snapshot of the current editor state.
     */
    Snapshot capture() const;

    /**
     * Returns a compact text fragment suitable for the system prompt.
     */
    QString toPromptFragment() const;

    /**
     * Returns in-memory file contents for prompt context.
     * @param maxChars Maximum number of characters to include.
     */
    QString fileContentsFragment(int maxChars = 60000) const;

};

}  // namespace qcai2
