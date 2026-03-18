/*! Declares the `/explain` slash command — sends selected editor code to the AI for explanation.
 */

#include "slash_command_registry.h"

#include "../context/editor_context.h"

#include <QFileInfo>

namespace qcai2
{

namespace
{

constexpr qsizetype k_max_selected_chars = 8000;

QString language_hint_for_file(const QString &file_path)
{
    if (file_path.isEmpty())
    {
        return {};
    }
    const QString suffix = QFileInfo(file_path).suffix().toLower();
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

void explain_command(const slash_command_invocation_t &invocation,
                     const slash_command_context_t &context)
{
    editor_context_t editor_ctx;
    const editor_context_t::snapshot_t snapshot = editor_ctx.capture();

    if (snapshot.selected_text.isEmpty())
    {
        if (context.log_message)
        {
            context.log_message(QStringLiteral("❌ No text selected in the editor."));
        }
        return;
    }

    // QTextCursor::selectedText() uses U+2029 paragraph separators instead of \n
    QString code = snapshot.selected_text;
    code.replace(QChar(0x2029), QLatin1Char('\n'));
    code.replace(QChar(0x2028), QLatin1Char('\n'));

    bool truncated = false;
    if (code.size() > k_max_selected_chars)
    {
        code.truncate(k_max_selected_chars);
        truncated = true;
    }

    QString prompt = QStringLiteral("Explain the following code");
    if (!snapshot.file_path.isEmpty())
    {
        prompt += QStringLiteral(" from `%1`").arg(snapshot.file_path);
    }
    if (!invocation.arguments.isEmpty())
    {
        prompt += QStringLiteral(". Focus on: %1").arg(invocation.arguments);
    }

    const QString lang = language_hint_for_file(snapshot.file_path);
    prompt += QStringLiteral(":\n\n```%1\n%2\n```").arg(lang, code);
    if (truncated)
    {
        prompt += QStringLiteral("\n\n*(selection truncated to %1 characters)*")
                      .arg(k_max_selected_chars);
    }

    if (context.goal_override != nullptr)
    {
        *context.goal_override = prompt;
    }
}

DECLARE_COMMAND(explain, "Explain the selected code in the editor.", explain_command)

}  // namespace qcai2
