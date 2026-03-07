#pragma once

#include <texteditor/codeassist/completionassistprovider.h>

namespace qcai2
{

class IAIProvider;

/**
 * Provides AI-powered code completions for editor assist requests.
 */
class AiCompletionProvider : public TextEditor::CompletionAssistProvider
{
    Q_OBJECT
public:
    /**
     * Creates a completion provider.
     * @param parent Owning QObject.
     */
    explicit AiCompletionProvider(QObject *parent = nullptr);

    /**
     * Creates a processor for the current assist request.
     * @param assistInterface Current editor assist context.
     * @return Async processor, or nullptr when completion is unavailable.
     */
    TextEditor::IAssistProcessor *
    createProcessor(const TextEditor::AssistInterface *assistInterface) const override;

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
    void setAiProvider(IAIProvider *provider)
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
     * Enables or disables AI completion.
     * @param enabled True to allow this provider to create processors.
     */
    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
    }

    /**
     * Returns whether AI completion is enabled.
     * @return True when the provider may serve requests.
     */
    bool isEnabled() const
    {
        return m_enabled;
    }

private:
    /** Non-owning AI backend used for completion requests. */
    IAIProvider *m_provider = nullptr;
    /** Default model name used when no completion-specific model is configured. */
    QString m_model;
    /** Enables or disables the provider. */
    bool m_enabled = true;
};

}  // namespace qcai2
