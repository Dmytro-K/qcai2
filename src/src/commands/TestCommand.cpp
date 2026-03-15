/*! Declares the `/test` slash command — asks the AI to generate tests for the current
 *  file or selected function.
 */

#include "SlashCommandRegistry.h"

#include "../context/EditorContext.h"

#include <QFileInfo>

namespace qcai2
{

namespace
{

constexpr qsizetype kMaxSelectedChars = 8000;

QString languageHintForFile(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        return {};
    }
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("cxx") ||
        suffix == QStringLiteral("cc") || suffix == QStringLiteral("h") ||
        suffix == QStringLiteral("hpp") || suffix == QStringLiteral("hxx"))
    {
        return QStringLiteral("cpp");
    }
    if (suffix == QStringLiteral("py"))
    {
        return QStringLiteral("python");
    }
    if (suffix == QStringLiteral("js") || suffix == QStringLiteral("ts") ||
        suffix == QStringLiteral("jsx") || suffix == QStringLiteral("tsx"))
    {
        return QStringLiteral("typescript");
    }
    if (suffix == QStringLiteral("rs"))
    {
        return QStringLiteral("rust");
    }
    return suffix;
}

}  // namespace

void testCommand(const SlashCommandInvocation &invocation, const SlashCommandContext &context)
{
    EditorContext editorCtx;
    const EditorContext::Snapshot snapshot = editorCtx.capture();

    QString code = snapshot.selectedText;
    if (!code.isEmpty())
    {
        code.replace(QChar(0x2029), QLatin1Char('\n'));
        code.replace(QChar(0x2028), QLatin1Char('\n'));

        if (code.size() > kMaxSelectedChars)
        {
            code.truncate(kMaxSelectedChars);
        }
    }

    const bool hasSelection = !code.isEmpty();
    const QString lang = languageHintForFile(snapshot.filePath);
    const QString fileName =
        snapshot.filePath.isEmpty() ? QString() : QFileInfo(snapshot.filePath).fileName();

    QString prompt;
    if (hasSelection)
    {
        prompt = QStringLiteral("Generate unit tests for the following code");
        if (!snapshot.filePath.isEmpty())
        {
            prompt += QStringLiteral(" from `%1`").arg(snapshot.filePath);
        }
        if (!invocation.arguments.isEmpty())
        {
            prompt += QStringLiteral(". %1").arg(invocation.arguments);
        }
        prompt += QStringLiteral(":\n\n```%1\n%2\n```").arg(lang, code);
    }
    else if (!snapshot.filePath.isEmpty())
    {
        prompt = QStringLiteral("Generate unit tests for the file `%1`").arg(snapshot.filePath);
        if (!invocation.arguments.isEmpty())
        {
            prompt += QStringLiteral(". %1").arg(invocation.arguments);
        }
        prompt += QStringLiteral(
            ".\n\nRead the file contents first, then create a comprehensive test suite.");
    }
    else
    {
        if (context.logMessage)
        {
            context.logMessage(
                QStringLiteral("❌ No file is open and no text is selected in the editor."));
        }
        return;
    }

    prompt += QStringLiteral("\n\nUse the appropriate testing framework for the language. "
                             "Create the test file using `apply_patch`.");

    if (context.goalOverride != nullptr)
    {
        *context.goalOverride = prompt;
    }
}

DECLARE_COMMAND(test, "Generate tests for the selected code or current file.", testCommand)

}  // namespace qcai2
