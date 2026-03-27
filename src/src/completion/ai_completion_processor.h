#pragma once

#include <texteditor/codeassist/iassistprocessor.h>

#include "../progress/agent_progress.h"
#include "../util/completion_detailed_log.h"

#include <QString>
#include <memory>

namespace qcai2
{

class iai_provider_t;
class chat_context_manager_t;
struct provider_usage_t;

/**
 * Runs an asynchronous AI completion request for a single assist session.
 */
class ai_completion_processor_t : public TextEditor::IAssistProcessor
{
public:
    /**
     * Creates a processor bound to a provider and model.
     * @param provider Non-owning AI backend.
     * @param model Model name used for the request.
     */
    ai_completion_processor_t(iai_provider_t *provider,
                              chat_context_manager_t *chat_context_manager, const QString &model);

    /**
     * Marks the processor as no longer alive for async callbacks.
     */
    ~ai_completion_processor_t() override;

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
    /**
     * Safely dispatches an async provider completion back to the processor.
     */
    static void dispatch_completion_response(ai_completion_processor_t *processor, int pos,
                                             const std::shared_ptr<bool> &alive,
                                             const QString &response, const QString &error,
                                             const provider_usage_t &usage);

    /**
     * Safely dispatches one async provider progress event back to the processor.
     */
    static void dispatch_progress_event(ai_completion_processor_t *processor,
                                        const std::shared_ptr<bool> &alive,
                                        const provider_raw_event_t &event);

    /**
     * Processes one async completion response.
     */
    void handle_completion_response(int pos, const QString &response, const QString &error,
                                    const provider_usage_t &usage);

    /**
     * Records one provider progress event into the detailed completion log.
     * @param event Provider progress event to record.
     */
    void handle_progress_event(const provider_raw_event_t &event);

    /**
     * Finalizes and appends the detailed completion log exactly once.
     * @param status Final request status.
     * @param response Final provider response text.
     * @param error Final error text.
     * @param usage Final provider usage counters.
     */
    void finalize_completion_log(const QString &status, const QString &response,
                                 const QString &error, const provider_usage_t &usage);

    /** Non-owning AI backend used for completion requests. */
    iai_provider_t *provider;

    /** Model name passed to the provider. */
    QString model;

    /** Shared persistent chat context used for lightweight completion retrieval. */
    chat_context_manager_t *chat_context_manager = nullptr;

    /** True while an async request is in flight. */
    bool request_running = false;

    /** True after cancellation was requested. */
    bool cancelled = false;

    /** Guards callbacks against use-after-free after the processor is destroyed. */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    /** Detailed per-request completion trace written under the launch-specific completion log
     * directory. */
    completion_detailed_log_t detailed_log;

    /** True after detailed logging has been initialized for the current request. */
    bool detailed_log_started = false;

    /** True after the detailed log has been finalized and appended to disk. */
    bool detailed_log_finished = false;

    /** Prefix snapshot used to build the current request. */
    QString request_prefix;

    /** Suffix snapshot used to build the current request. */
    QString request_suffix;
};

}  // namespace qcai2
