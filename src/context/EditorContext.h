#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace qcai2
{

// Gathers context from the active Qt Creator editor / project.
class EditorContext : public QObject
{
    Q_OBJECT
public:
    explicit EditorContext(QObject *parent = nullptr);

    struct Snapshot
    {
        QString filePath;
        QString selectedText;
        int cursorLine = -1;
        int cursorColumn = -1;
        QStringList openFiles;

        // Project info
        QString projectName;
        QString projectDir;
        QString projectFilePath;
        QString buildDir;
        QString buildType;  // Debug, Release, Profile, Unknown
        QString kitName;
        QString compilerName;
        QString compilerPath;
        QString targetName;
    };

    // Take a snapshot of the current editor state.
    Snapshot capture() const;

    // Return a compact string for inclusion in the LLM system prompt.
    QString toPromptFragment() const;

    // Return file contents for the system prompt context.
    // Includes active file + open tabs, up to maxChars total.
    QString fileContentsFragment(int maxChars = 60000) const;
};

}  // namespace qcai2
