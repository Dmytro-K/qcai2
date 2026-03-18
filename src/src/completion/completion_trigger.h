#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace TextEditor
{
class TextEditorWidget;
}

namespace Qcai2
{

class ai_completion_provider_t;

/**
 * Debounces editor changes and auto-triggers AI completion when typing pauses.
 */
class completion_trigger_t : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a trigger bound to a completion provider.
     * @param provider Non-owning completion provider invoked after the debounce delay.
     * @param parent Owning QObject.
     */
    explicit completion_trigger_t(ai_completion_provider_t *provider, QObject *parent = nullptr);

    /**
     * Starts monitoring an editor for completion triggers.
     * @param editor Editor widget to monitor.
     */
    void attachToEditor(TextEditor::TextEditorWidget *editor);

private:
    /**
     * Handles a queued editor content change.
     * @param editor Editor whose contents changed.
     */
    void onTextChanged(TextEditor::TextEditorWidget *editor);

    /**
     * Counts word characters immediately before the cursor.
     * @param editor Editor whose cursor position is inspected.
     * @return Length of the current identifier fragment.
     */
    int wordLengthAtCursor(TextEditor::TextEditorWidget *editor) const;

    /** Non-owning provider used to invoke completion. */
    ai_completion_provider_t *provider;

    /** Shared debounce timer for delayed auto-trigger requests. */
    QTimer timer;

    /** Startup guard used to suppress triggers during initial editor loading. */
    QElapsedTimer startupTimer;

    /** Editor waiting for the current debounce timeout. */
    TextEditor::TextEditorWidget *pendingEditor = nullptr;

    /** Editors already connected to change notifications. */
    QSet<TextEditor::TextEditorWidget *> attached;

    /** Editors that have emitted their first post-attach contentsChanged signal. */
    QSet<TextEditor::TextEditorWidget *> initialized;
};

}  // namespace Qcai2
