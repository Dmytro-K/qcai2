#pragma once

#include <QObject>
#include <QTimer>
#include <QSet>
#include <QElapsedTimer>

namespace TextEditor {
class TextEditorWidget;
}

namespace Qcai2 {

class AiCompletionProvider;

// Monitors typing in editors and triggers AI completion after N chars + M ms delay.
// Uses contentsChanged signal to avoid conflicts with FakeVim etc.
class CompletionTrigger : public QObject
{
    Q_OBJECT
public:
    explicit CompletionTrigger(AiCompletionProvider *provider, QObject *parent = nullptr);

    void attachToEditor(TextEditor::TextEditorWidget *editor);

private:
    void onTextChanged(TextEditor::TextEditorWidget *editor);
    int wordLengthAtCursor(TextEditor::TextEditorWidget *editor) const;

    AiCompletionProvider *m_provider;
    QTimer m_timer;
    QElapsedTimer m_startupTimer;
    TextEditor::TextEditorWidget *m_pendingEditor = nullptr;
    QSet<TextEditor::TextEditorWidget *> m_attached;
    QSet<TextEditor::TextEditorWidget *> m_initialized;
};

} // namespace Qcai2
