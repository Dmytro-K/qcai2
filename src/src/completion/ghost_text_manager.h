#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <memory>

namespace qompi
{
class completion_session_t;
}  // namespace qompi

namespace TextEditor
{
class TextEditorWidget;
}

namespace qcai2
{

class iai_provider_t;
class chat_context_manager_t;
class clangd_service_t;
class completion_detailed_log_t;
struct provider_usage_t;

/**
 * Stores one frozen completion snapshot captured immediately after user typing.
 */
struct pending_completion_snapshot_t
{
    /** Cursor position right after the triggering edit. */
    int cursor_position = 0;

    /** Prefix text captured from the document at trigger time. */
    QString prefix;

    /** Suffix text captured from the document at trigger time. */
    QString suffix;
};

/**
 * Tracks one in-flight streaming ghost-text response.
 */
struct streaming_completion_state_t
{
    /** Accumulated insertion-only streamed text from MESSAGE_DELTA events. */
    QString accumulated_completion;

    /** Last stable insertion-only prefix already rendered into the editor suggestion. */
    QString last_rendered_completion;
};

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
     * Sets the provider pool available to ghost-text completion requests.
     * @param providers Non-owning provider instances dedicated to completion.
     */
    void set_providers(const QList<iai_provider_t *> &providers)
    {
        this->providers = providers;
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
     * Sets the shared clangd service used for local semantic completion context.
     * @param service Reusable clangd integration service.
     */
    void set_clangd_service(clangd_service_t *service)
    {
        this->clangd_service = service;
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

public:
    /**
     * Resolves the effective provider for the next ghost-text request.
     * @return Non-owning provider selected from the completion provider pool.
     */
    iai_provider_t *resolve_provider() const;

    /**
     * Processes one ghost-text completion response.
     */
    void
    handle_completion_response(TextEditor::TextEditorWidget *editor, int pos,
                               const QString &prefix, const QString &suffix,
                               const QString &response, const QString &error,
                               const provider_usage_t &usage,
                               const std::shared_ptr<completion_detailed_log_t> &detailed_log);

    /**
     * Handles editor content changes and restarts the debounce timer.
     * @param editor Editor whose contents changed.
     * @param cursor_position Cursor position that must remain stable until debounce fires.
     */
    void on_contents_changed(TextEditor::TextEditorWidget *editor, int cursor_position);

    /**
     * Requests a ghost-text completion for the current cursor position.
     * @param editor Editor that will receive the suggestion.
     */
    void request_completion(TextEditor::TextEditorWidget *editor);

    /**
     * Shows one inline suggestion for either a partial or final completion.
     * @param editor Editor that owns the suggestion.
     * @param pos Cursor position frozen when the request started.
     * @param prefix Frozen prefix used to build the request.
     * @param suffix Frozen suffix used to build the request.
     * @param completion Normalized insertion-only completion text to render.
     * @param usage Provider usage metadata used for final error/status reporting.
     * @param detailed_log Detailed completion log for this request.
     * @param final_update True when this is the final completion callback.
     * @return True when the suggestion was rendered successfully.
     */
    bool show_completion_suggestion(TextEditor::TextEditorWidget *editor, int pos,
                                    const QString &prefix, const QString &suffix,
                                    const QString &completion, const provider_usage_t &usage,
                                    const std::shared_ptr<completion_detailed_log_t> &detailed_log,
                                    bool final_update);

private:
    /**
     * Removes wrapper markup from a model response.
     * @param raw Raw completion text returned by the provider.
     * @return Clean text ready to display inline.
     */
    static QString clean_completion(const QString &raw, const QString &prefix,
                                    const QString &suffix);

    /** Non-owning AI backend used for inline suggestions. */
    iai_provider_t *provider = nullptr;

    /** Non-owning provider pool dedicated to completion requests. */
    QList<iai_provider_t *> providers;

    /** Default model name used when settings do not override it. */
    QString model;

    /** Shared persistent chat context used for lightweight completion retrieval. */
    chat_context_manager_t *chat_context_manager = nullptr;

    /** Shared clangd service used for local semantic completion context. */
    clangd_service_t *clangd_service = nullptr;

    /** Enables or disables inline suggestions globally. */
    bool enabled = true;

    /** Per-editor debounce timers keyed by editor widget. */
    QHash<TextEditor::TextEditorWidget *, QTimer *> debounce_timers;

    /** Last edit position recorded for each attached editor. */
    QHash<TextEditor::TextEditorWidget *, int> last_edit_positions;

    /** Frozen prompt snapshots captured when a user edit starts a debounce window. */
    QHash<TextEditor::TextEditorWidget *, pending_completion_snapshot_t> pending_snapshots;

    /** Reusable qompi sessions keyed by editor for latest-wins ghost-text requests. */
    QHash<TextEditor::TextEditorWidget *, std::shared_ptr<qompi::completion_session_t>>
        qompi_sessions;

    /** Provider identifier bound to each reusable qompi session. */
    QHash<TextEditor::TextEditorWidget *, QString> qompi_session_provider_ids;

    /** Guards async callbacks against use-after-free after destruction. */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

}  // namespace qcai2
