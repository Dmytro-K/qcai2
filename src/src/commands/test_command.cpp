/*! Declares the `/test` slash command — asks the AI to generate tests for the current
 *  file or selected function.
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

void test_command(const slash_command_invocation_t &invocation,
                  const slash_command_context_t &context)
{
    editor_context_t editor_ctx;
    const editor_context_t::snapshot_t snapshot = editor_ctx.capture();

    QString code = snapshot.selected_text;
    if (!code.isEmpty())
    {
        code.replace(QChar(0x2029), QLatin1Char('\n'));
        code.replace(QChar(0x2028), QLatin1Char('\n'));

        if (code.size() > k_max_selected_chars)
        {
            code.truncate(k_max_selected_chars);
        }
    }

    const bool has_selection = !code.isEmpty();
    const QString lang = language_hint_for_file(snapshot.file_path);
    QString prompt;
    if (has_selection)
    {
        prompt = QStringLiteral("Generate unit tests for the following code");
        if (!snapshot.file_path.isEmpty())
        {
            prompt += QStringLiteral(" from `%1`").arg(snapshot.file_path);
        }
        if (!invocation.arguments.isEmpty())
        {
            prompt += QStringLiteral(". %1").arg(invocation.arguments);
        }
        prompt += QStringLiteral(":\n\n```%1\n%2\n```").arg(lang, code);
    }
    else if (!snapshot.file_path.isEmpty())
    {
        prompt = QStringLiteral("Generate unit tests for the file `%1`").arg(snapshot.file_path);
        if (!invocation.arguments.isEmpty())
        {
            prompt += QStringLiteral(". %1").arg(invocation.arguments);
        }
        prompt += QStringLiteral(
            ".\n\nRead the file contents first, then create a comprehensive test suite.");
    }
    else
    {
        if (context.log_message)
        {
            context.log_message(
                QStringLiteral("❌ No file is open and no text is selected in the editor."));
        }
        return;
    }

    prompt += QStringLiteral("\n\nUse the appropriate testing framework for the language. "
                             "Create the test file using `apply_patch`.");

    if (context.goal_override != nullptr)
    {
        *context.goal_override = prompt;
    }
}

DECLARE_COMMAND(test, "Generate tests for the selected code or current file.", test_command)

}  // namespace qcai2
