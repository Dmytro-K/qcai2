#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

namespace TextEditor
{
class TextEditorWidget;
}

namespace qcai2
{

class IAIProvider;

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
