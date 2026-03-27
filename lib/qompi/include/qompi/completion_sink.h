/*! @file
    @brief Declares the consumer callback interface for streamed completion events.
*/

#pragma once

#include "completion_types.h"

#include <string>

namespace qompi
{

/**
 * Receives lifecycle callbacks for one completion request.
 *
 * Implementations are typically thin adapters that forward events to editor code,
 * tests, or higher-level session observers.
 */
class completion_sink_t
{
public:
    /** Virtual destructor for polymorphic use. */
    virtual ~completion_sink_t() = default;

    /**
     * Called when the backend has accepted the request and started work.
     * @param request_id Correlation identifier for the request.
     */
    virtual void on_started(const std::string &request_id)
    {
        (void)request_id;
    }

    /**
     * Called when backend-specific debug information is available for one request.
     *
     * This hook is intentionally optional and request-scoped so editor integrations can surface
     * transport or normalization diagnostics without coupling qompi core to any concrete logging
     * framework.
     *
     * @param stage Stable stage identifier.
     * @param message Human-readable stage details.
     */
    virtual void on_debug_event(const std::string &stage, const std::string &message)
    {
        (void)stage;
        (void)message;
    }

    /**
     * Called when one normalized streamed completion chunk is available.
     * @param chunk Incremental update.
     */
    virtual void on_chunk(const completion_chunk_t &chunk) = 0;

    /**
     * Called once when the request completes successfully.
     * @param result Final completion result.
     */
    virtual void on_completed(const completion_result_t &result) = 0;

    /**
     * Called once when the request fails.
     * @param error Surfaced completion error.
     */
    virtual void on_failed(const completion_error_t &error) = 0;
};

}  // namespace qompi
