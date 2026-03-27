#pragma once

#include <QList>
#include <texteditor/codeassist/completionassistprovider.h>

namespace qcai2
{

class iai_provider_t;
class chat_context_manager_t;
class clangd_service_t;

/**
 * Provides AI-powered code completions for editor assist requests.
 */
class ai_completion_provider_t : public TextEditor::CompletionAssistProvider
{
    Q_OBJECT
public:
    /**
     * Creates a completion provider.
     * @param parent Owning QObject.
     */
    explicit ai_completion_provider_t(QObject *parent = nullptr);

    /**
     * Creates a processor for the current assist request.
     * @param assistInterface Current editor assist context.
     * @return Async processor, or nullptr when completion is unavailable.
     */
    TextEditor::IAssistProcessor *
    createProcessor(const TextEditor::AssistInterface *assist_interface) const override;

    /**
     * Returns the activation sequence length inspected by this provider.
     * @return Number of trailing characters considered for activation.
     */
    int activationCharSequenceLength() const override;

    /**
     * Checks whether the latest typed sequence should trigger completion.
     * @param sequence Recently typed characters.
     * @return True when AI completion should be started.
     */
    bool isActivationCharSequence(const QString &sequence) const override;

    /**
     * Sets the shared AI provider.
     * @param provider Provider used to fetch completion suggestions.
     */
    void set_ai_provider(iai_provider_t *provider)
    {
        this->provider = provider;
    }

    /**
     * Sets the provider pool available to completion requests.
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
     * Sets the persistent chat context manager used for tiny completion context retrieval.
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
     * Enables or disables AI completion.
     * @param enabled True to allow this provider to create processors.
     */
    void set_enabled(bool enabled)
    {
        this->enabled = enabled;
    }

    /**
     * Returns whether AI completion is enabled.
     * @return True when the provider may serve requests.
     */
    bool is_enabled() const
    {
        return this->enabled;
    }

private:
    /**
     * Resolves the effective provider for the next completion request.
     * @return Non-owning provider selected from the completion provider pool.
     */
    iai_provider_t *resolve_provider() const;

    /** Non-owning AI backend used for completion requests. */
    iai_provider_t *provider = nullptr;

    /** Non-owning provider pool dedicated to completion requests. */
    QList<iai_provider_t *> providers;

    /** Default model name used when no completion-specific model is configured. */
    QString model;

    /** Shared persistent chat context used for lightweight completion context. */
    chat_context_manager_t *chat_context_manager = nullptr;

    /** Shared clangd service used for local semantic completion context. */
    clangd_service_t *clangd_service = nullptr;

    /** Enables or disables the provider. */
    bool enabled = true;
};

}  // namespace qcai2
