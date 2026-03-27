/*! @file
    @brief Declares the public handle for one inflight completion operation.
*/

#pragma once

#include "completion_types.h"

#include <functional>
#include <memory>
#include <string>

namespace qompi
{

/**
 * Represents one inflight completion request that can be observed and cancelled.
 */
class completion_operation_t
{
public:
    /** Virtual destructor for polymorphic use. */
    virtual ~completion_operation_t() = default;

    /**
     * Returns the stable correlation identifier for this operation.
     * @return Request identifier.
     */
    virtual std::string request_id() const = 0;

    /**
     * Returns the current lifecycle state.
     * @return Current state.
     */
    virtual completion_operation_state_t state() const = 0;

    /**
     * Requests cancellation of the operation.
     */
    virtual void cancel() = 0;

    /**
     * Returns whether the operation has already reached a terminal state.
     * @return True when completed, failed, or cancelled.
     */
    bool is_terminal() const;
};

/**
 * Creates a lightweight operation handle backed by an optional cancellation callback.
 * @param request_id Stable request identifier for the handle.
 * @param cancel_callback Callback invoked at most once when cancellation is requested.
 * @return Shared operation handle suitable for callback-backed backends.
 */
std::shared_ptr<completion_operation_t>
create_callback_completion_operation(std::string request_id,
                                     std::function<void()> cancel_callback = {});

}  // namespace qompi
