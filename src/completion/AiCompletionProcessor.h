#pragma once

#include <texteditor/codeassist/iassistprocessor.h>

#include <QString>
#include <memory>

namespace qcai2
{

class IAIProvider;

/**
 * Runs an asynchronous AI completion request for a single assist session.
 */
class AiCompletionProcessor : public TextEditor::IAssistProcessor
{
public:
    /**
     * Creates a processor bound to a provider and model.
     * @param provider Non-owning AI backend.
     * @param model Model name used for the request.
     */
    AiCompletionProcessor(IAIProvider *provider, const QString &model);

    /**
     * Marks the processor as no longer alive for async callbacks.
     */
    ~AiCompletionProcessor() override;

    /**
     * Returns whether a completion request is still running.
     * @return True while the async provider call is active.
     */
    bool running() override;

    /**
     * Cancels the active completion request.
     */
    void cancel() override;

protected:
    /**
     * Starts the async completion workflow.
     * @return Always nullptr because proposals are delivered asynchronously.
     */
    TextEditor::IAssistProposal *perform() override;

private:
    /** Non-owning AI backend used for completion requests. */
    IAIProvider *m_provider;

    /** Model name passed to the provider. */
    QString m_model;

    /** True while an async request is in flight. */
    bool m_running = false;

    /** True after cancellation was requested. */
    bool m_cancelled = false;

    /** Guards callbacks against use-after-free after the processor is destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

};

}  // namespace qcai2
