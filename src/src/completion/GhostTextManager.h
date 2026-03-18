#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>

namespace TextEditor
{
class TextEditorWidget;
}

namespace qcai2
{

class IAIProvider;
class ChatContextManager;
struct ProviderUsage;

/**
 * Manages debounced inline ghost-text suggestions for attached editors.
 */
class GhostTextManager : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a ghost-text manager.
     * @param parent Owning QObject.
     */
    explicit GhostTextManager(QObject *parent = nullptr);

    /**
     * Marks outstanding async callbacks as stale on destruction.
     */
    ~GhostTextManager() override;

    /**
     * Sets the shared AI provider.
     * @param provider Provider used to fetch ghost-text suggestions.
     */
    void setProvider(IAIProvider *provider)
    {
        m_provider = provider;
    }

    /**
     * Sets the default model for completion requests.
     * @param model Model name used when settings do not override it.
     */
    void setModel(const QString &model)
    {
        m_model = model;
    }

    /**
     * Sets the persistent chat context manager used for lightweight completion context.
     * @param manager Shared context manager instance.
     */
    void setChatContextManager(ChatContextManager *manager)
    {
        m_chatContextManager = manager;
    }

    /**
     * Enables or disables ghost-text suggestions.
     * @param enabled True to allow suggestion requests.
     */
    void setEnabled(bool enabled);

    /**
     * Returns whether ghost-text suggestions are enabled.
     * @return True when requests may be issued.
     */
    bool isEnabled() const
    {
        return m_enabled;
    }

    /**
     * Starts monitoring an editor for ghost-text suggestions.
     * @param editor Editor widget to attach to.
     */
    void attachToEditor(TextEditor::TextEditorWidget *editor);

private:
    /**
     * Safely dispatches one async completion response back to the manager.
     */
    static void dispatchCompletionResponse(GhostTextManager *manager,
                                           QPointer<TextEditor::TextEditorWidget> editor, int pos,
                                           const std::shared_ptr<bool> &alive,
                                           const QString &response, const QString &error,
                                           const ProviderUsage &usage);

    /**
     * Processes one ghost-text completion response.
     */
    void handleCompletionResponse(TextEditor::TextEditorWidget *editor, int pos,
                                  const QString &response, const QString &error);

    /**
     * Handles editor content changes and restarts the debounce timer.
     * @param editor Editor whose contents changed.
     */
    void onContentsChanged(TextEditor::TextEditorWidget *editor);

    /**
     * Requests a ghost-text completion for the current cursor position.
     * @param editor Editor that will receive the suggestion.
     */
    void requestCompletion(TextEditor::TextEditorWidget *editor);

    /**
     * Removes wrapper markup from a model response.
     * @param raw Raw completion text returned by the provider.
     * @return Clean text ready to display inline.
     */
    static QString cleanCompletion(const QString &raw);

    /** Non-owning AI backend used for inline suggestions. */
    IAIProvider *m_provider = nullptr;

    /** Default model name used when settings do not override it. */
    QString m_model;

    /** Shared persistent chat context used for lightweight completion retrieval. */
    ChatContextManager *m_chatContextManager = nullptr;

    /** Enables or disables inline suggestions globally. */
    bool m_enabled = true;

    /** Per-editor debounce timers keyed by editor widget. */
    QHash<TextEditor::TextEditorWidget *, QTimer *> m_debounceTimers;

    /** Last edit position recorded for each attached editor. */
    QHash<TextEditor::TextEditorWidget *, int> m_lastEditPositions;

    /** Guards async callbacks against use-after-free after destruction. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}  // namespace qcai2
