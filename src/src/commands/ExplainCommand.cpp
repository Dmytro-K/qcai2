/*! Declares the `/explain` slash command — sends selected editor code to the AI for explanation.
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
        suffix == QStringLiteral("cc"))
    {
        return QStringLiteral("cpp");
    }
    if (suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp") ||
        suffix == QStringLiteral("hxx"))
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

void explainCommand(const SlashCommandInvocation &invocation, const SlashCommandContext &context)
{
    EditorContext editorCtx;
    const EditorContext::Snapshot snapshot = editorCtx.capture();

    if (snapshot.selectedText.isEmpty())
    {
        if (context.logMessage)
        {
            context.logMessage(QStringLiteral("❌ No text selected in the editor."));
        }
        return;
    }

    // QTextCursor::selectedText() uses U+2029 paragraph separators instead of \n
    QString code = snapshot.selectedText;
    code.replace(QChar(0x2029), QLatin1Char('\n'));
    code.replace(QChar(0x2028), QLatin1Char('\n'));

    bool truncated = false;
    if (code.size() > kMaxSelectedChars)
    {
        code.truncate(kMaxSelectedChars);
        truncated = true;
    }

    QString prompt = QStringLiteral("Explain the following code");
    if (!snapshot.filePath.isEmpty())
    {
        prompt += QStringLiteral(" from `%1`").arg(snapshot.filePath);
    }
    if (!invocation.arguments.isEmpty())
    {
        prompt += QStringLiteral(". Focus on: %1").arg(invocation.arguments);
    }

    const QString lang = languageHintForFile(snapshot.filePath);
    prompt += QStringLiteral(":\n\n```%1\n%2\n```").arg(lang, code);
    if (truncated)
    {
        prompt +=
            QStringLiteral("\n\n*(selection truncated to %1 characters)*").arg(kMaxSelectedChars);
    }

    if (context.goalOverride != nullptr)
    {
        *context.goalOverride = prompt;
    }
}

DECLARE_COMMAND(explain, "Explain the selected code in the editor.", explainCommand)

}  // namespace qcai2
