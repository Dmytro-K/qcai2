/*! @file
    @brief Implements delayed auto-triggering for editor AI completion.
*/

#include "CompletionTrigger.h"
#include "../settings/Settings.h"
#include "../util/Logger.h"
#include "AiCompletionProvider.h"

#include <texteditor/codeassist/assistenums.h>
#include <texteditor/texteditor.h>

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>

namespace Qcai2
{

completion_trigger_t::completion_trigger_t(ai_completion_provider_t *provider, QObject *parent)
    : QObject(parent), provider(provider)
{
    this->startupTimer.start();

    this->timer.setSingleShot(true);
    connect(&this->timer, &QTimer::timeout, this, [this]() {
        if (!this->pendingEditor || !this->provider || !this->provider->isEnabled())
            return;

        // Don't trigger during startup (first 10 seconds)
        if (this->startupTimer.elapsed() < 10000)
        {
            this->pendingEditor = nullptr;
            return;
        }

        // Safety: editor must be visible and have focus
        if (!this->pendingEditor->isVisible() || !this->pendingEditor->hasFocus())
        {
            this->pendingEditor = nullptr;
            return;
        }

        const auto &s = settings();
        const int wordLen = this->wordLengthAtCursor(this->pendingEditor);
        if (wordLen >= s.completionMinChars)
        {
            QCAI_DEBUG(
                "Trigger",
                QStringLiteral("Auto-triggering completion: %1 chars at cursor").arg(wordLen));
            // Pass our provider explicitly to avoid nullptr dereference in CodeAssistant
            this->pendingEditor->invokeAssist(TextEditor::Completion, this->provider);
        }
        this->pendingEditor = nullptr;
    });
}

void completion_trigger_t::attachToEditor(TextEditor::TextEditorWidget *editor)
{
    if (!editor || this->attached.contains(editor))
        return;

    this->attached.insert(editor);

    // Use contentsChanged — safe with FakeVim and other event filter plugins.
    // Use QueuedConnection to avoid triggering during initial document load.
    connect(
        editor->document(), &QTextDocument::contentsChanged, this,
        [this, editor]() { this->onTextChanged(editor); }, Qt::QueuedConnection);

    // Clean up when editor is destroyed
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        this->attached.remove(editor);
        this->initialized.remove(editor);
        if (this->pendingEditor == editor)
        {
            this->pendingEditor = nullptr;
            this->timer.stop();
        }
    });

    QCAI_DEBUG("Trigger", QStringLiteral("Attached to editor widget (via contentsChanged)"));
}

void completion_trigger_t::onTextChanged(TextEditor::TextEditorWidget *editor)
{
    // Skip the first contentsChanged per editor (initial document load)
    if (!this->initialized.contains(editor))
    {
        this->initialized.insert(editor);
        return;
    }

    if (!this->provider || !this->provider->isEnabled())
        return;

    const auto &s = settings();
    if (!s.aiCompletionEnabled)
        return;

    this->pendingEditor = editor;
    this->timer.start(s.completionDelayMs);
}

int completion_trigger_t::wordLengthAtCursor(TextEditor::TextEditorWidget *editor) const
{
    QTextCursor tc = editor->textCursor();
    const int pos = tc.position();
    const QString lineText = tc.block().text();
    const int col = pos - tc.block().position();

    // Count word chars backwards from cursor position
    int len = 0;
    for (int i = col - 1; i >= 0; --i)
    {
        const QChar c = lineText.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            ++len;
        else
            break;
    }
    return len;
}

}  // namespace Qcai2
