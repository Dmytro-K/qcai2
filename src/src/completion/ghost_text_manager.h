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

class iai_provider_t;
class chat_context_manager_t;
struct provider_usage_t;

/**
 * Manages debounced inline ghost-text suggestions for attached editors.
 */
class ghost_text_manager_t : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a ghost-text manager.
     * @param parent Owning QObject.
     */
    explicit ghost_text_manager_t(QObject *parent = nullptr);

    /**
     * Marks outstanding async callbacks as stale on destruction.
     */
    ~ghost_text_manager_t() override;

    /**
     * Sets the shared AI provider.
     * @param provider Provider used to fetch ghost-text suggestions.
     */
    void set_provider(iai_provider_t *provider)
    {
        this->provider = provider;
    }

    /**
     * Sets the default model for completion requests.
     * @param model Model name used when settings do not override it.
     */
    void set_model(const QString &model)
    {
        this->model = model;
    }

    /**
     * Sets the persistent chat context manager used for lightweight completion context.
     * @param manager Shared context manager instance.
     */
    void set_chat_context_manager(chat_context_manager_t *manager)
    {
        this->chat_context_manager = manager;
    }

    /**
     * Enables or disables ghost-text suggestions.
     * @param enabled True to allow suggestion requests.
     */
    void set_enabled(bool enabled);

    /**
     * Returns whether ghost-text suggestions are enabled.
     * @return True when requests may be issued.
     */
    bool is_enabled() const
    {
        return this->enabled;
    }

    /**
     * Starts monitoring an editor for ghost-text suggestions.
     * @param editor Editor widget to attach to.
     */
    void attach_to_editor(TextEditor::TextEditorWidget *editor);

private:
    /**
     * Safely dispatches one async completion response back to the manager.
     */
    static void dispatch_completion_response(ghost_text_manager_t *manager,
                                             QPointer<TextEditor::TextEditorWidget> editor,
                                             int pos, const std::shared_ptr<bool> &alive,
                                             const QString &response, const QString &error,
                                             const provider_usage_t &usage);

    /**
     * Processes one ghost-text completion response.
     */
    void handle_completion_response(TextEditor::TextEditorWidget *editor, int pos,
                                    const QString &response, const QString &error);

    /**
     * Handles editor content changes and restarts the debounce timer.
     * @param editor Editor whose contents changed.
     */
    void on_contents_changed(TextEditor::TextEditorWidget *editor);

    /**
     * Requests a ghost-text completion for the current cursor position.
     * @param editor Editor that will receive the suggestion.
     */
    void request_completion(TextEditor::TextEditorWidget *editor);

    /**
     * Removes wrapper markup from a model response.
     * @param raw Raw completion text returned by the provider.
     * @return Clean text ready to display inline.
     */
    static QString clean_completion(const QString &raw);

    /** Non-owning AI backend used for inline suggestions. */
    iai_provider_t *provider = nullptr;

    /** Default model name used when settings do not override it. */
    QString model;

    /** Shared persistent chat context used for lightweight completion retrieval. */
    chat_context_manager_t *chat_context_manager = nullptr;

    /** Enables or disables inline suggestions globally. */
    bool enabled = true;

    /** Per-editor debounce timers keyed by editor widget. */
    QHash<TextEditor::TextEditorWidget *, QTimer *> debounce_timers;

    /** Last edit position recorded for each attached editor. */
    QHash<TextEditor::TextEditorWidget *, int> last_edit_positions;

    /** Guards async callbacks against use-after-free after destruction. */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

}  // namespace qcai2
