/*! @file
    @brief Declares the editor-scoped session interface for autocomplete requests.
*/

#pragma once

#include "completion_operation.h"
#include "completion_sink.h"
#include "completion_types.h"

#include <memory>

namespace qompi
{

class completion_backend_t;

/**
 * Represents one caller-scoped autocomplete session.
 *
 * Sessions own request sequencing policy such as latest-wins cancellation and stale-result
 * suppression, while individual providers remain unaware of editor-level semantics.
 */
class completion_session_t
{
public:
    /** Virtual destructor for polymorphic use. */
    virtual ~completion_session_t() = default;

    /**
     * Starts one completion request in the session.
     * @param request Normalized request data.
     * @param options Per-request provider overrides.
     * @param sink Consumer for lifecycle callbacks.
     * @return Handle for the inflight operation.
     */
    virtual std::shared_ptr<completion_operation_t>
    request_completion(const completion_request_t &request, const completion_options_t &options,
                       std::shared_ptr<completion_sink_t> sink) = 0;

    /**
     * Cancels the currently active request, if any.
     */
    virtual void cancel_active() = 0;
};

/**
 * Creates the default latest-wins session implementation for one backend.
 * @param backend Backend used to execute provider requests.
 * @return Newly created session.
 */
std::shared_ptr<completion_session_t>
create_latest_wins_completion_session(std::shared_ptr<completion_backend_t> backend);

}  // namespace qompi
