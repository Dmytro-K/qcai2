/*! @file
    @brief Declares the provider/backend extension seam used by qompi sessions.
*/

#pragma once

#include "completion_operation.h"
#include "completion_sink.h"
#include "completion_types.h"

#include <memory>

namespace qompi
{

/**
 * Starts provider-specific completion work for one normalized request.
 *
 * This interface intentionally isolates transport and provider implementation details
 * from the session lifecycle layer so they can be replaced independently of Qt.
 */
class completion_backend_t
{
public:
    /** Virtual destructor for polymorphic use. */
    virtual ~completion_backend_t() = default;

    /**
     * Starts one completion request.
     * @param request Normalized request data.
     * @param options Per-request overrides.
     * @param sink Receiver for backend lifecycle events.
     * @return Operation handle that can cancel provider work.
     */
    virtual std::shared_ptr<completion_operation_t>
    start_completion(const completion_request_t &request, const completion_options_t &options,
                     std::shared_ptr<completion_sink_t> sink) = 0;
};

}  // namespace qompi
