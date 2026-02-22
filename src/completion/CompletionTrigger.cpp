#include "CompletionTrigger.h"
#include "AiCompletionProvider.h"
#include "../settings/Settings.h"
#include "../util/Logger.h"

#include <texteditor/texteditor.h>
#include <texteditor/codeassist/assistenums.h>

#include <QTextCursor>
#include <QTextDocument>
#include <QPlainTextEdit>

namespace Qcai2 {

CompletionTrigger::CompletionTrigger(AiCompletionProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider)
{
    m_startupTimer.start();

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!m_pendingEditor || !m_provider || !m_provider->isEnabled())
            return;

        // Don't trigger during startup (first 10 seconds)
        if (m_startupTimer.elapsed() < 10000) {
            m_pendingEditor = nullptr;
            return;
        }

        // Safety: editor must be visible and have focus
        if (!m_pendingEditor->isVisible() || !m_pendingEditor->hasFocus()) {
            m_pendingEditor = nullptr;
            return;
        }

        const auto &s = settings();
        const int wordLen = wordLengthAtCursor(m_pendingEditor);
        if (wordLen >= s.completionMinChars) {
            QCAI_DEBUG("Trigger", QStringLiteral("Auto-triggering completion: %1 chars at cursor")
                .arg(wordLen));
            // Pass our provider explicitly to avoid nullptr dereference in CodeAssistant
            m_pendingEditor->invokeAssist(TextEditor::Completion, m_provider);
        }
        m_pendingEditor = nullptr;
    });
}

void CompletionTrigger::attachToEditor(TextEditor::TextEditorWidget *editor)
{
    if (!editor || m_attached.contains(editor))
        return;

    m_attached.insert(editor);

    // Use contentsChanged — safe with FakeVim and other event filter plugins.
    // Use QueuedConnection to avoid triggering during initial document load.
    connect(editor->document(), &QTextDocument::contentsChanged, this, [this, editor]() {
        onTextChanged(editor);
    }, Qt::QueuedConnection);

    // Clean up when editor is destroyed
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        m_attached.remove(editor);
        m_initialized.remove(editor);
        if (m_pendingEditor == editor) {
            m_pendingEditor = nullptr;
            m_timer.stop();
        }
    });

    QCAI_DEBUG("Trigger", QStringLiteral("Attached to editor widget (via contentsChanged)"));
}

void CompletionTrigger::onTextChanged(TextEditor::TextEditorWidget *editor)
{
    // Skip the first contentsChanged per editor (initial document load)
    if (!m_initialized.contains(editor)) {
        m_initialized.insert(editor);
        return;
    }

    if (!m_provider || !m_provider->isEnabled())
        return;

    const auto &s = settings();
    if (!s.aiCompletionEnabled)
        return;

    m_pendingEditor = editor;
    m_timer.start(s.completionDelayMs);
}

int CompletionTrigger::wordLengthAtCursor(TextEditor::TextEditorWidget *editor) const
{
    QTextCursor tc = editor->textCursor();
    const int pos = tc.position();
    const QString lineText = tc.block().text();
    const int col = pos - tc.block().position();

    // Count word chars backwards from cursor position
    int len = 0;
    for (int i = col - 1; i >= 0; --i) {
        const QChar c = lineText.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            ++len;
        else
            break;
    }
    return len;
}

} // namespace Qcai2
