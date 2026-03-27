/*! @file
    @brief Declares an event-loop abstraction for dispatching completion callbacks.
*/

#pragma once

#include <functional>
#include <memory>

namespace qompi::detail
{

/**
 * Dispatches one callback onto an implementation-specific event loop or execution context.
 */
class callback_dispatcher_t
{
public:
    /** Virtual destructor for polymorphic use. */
    virtual ~callback_dispatcher_t() = default;

    /**
     * Enqueues one callback for later execution.
     * @param callback Callback to execute.
     */
    virtual void dispatch(std::function<void()> callback) = 0;
};

}  // namespace qompi::detail
