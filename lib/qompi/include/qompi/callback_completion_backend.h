/*! @file
    @brief Declares a generic callback-backed completion backend for plugin-supplied executors.
*/

#pragma once

#include "completion_backend.h"

#include <functional>
#include <memory>

namespace qompi
{

/**
 * Function signature used to start one provider-backed completion request.
 */
using completion_start_callback_t = std::function<std::shared_ptr<completion_operation_t>(
    const completion_request_t &request, const completion_options_t &options,
    std::shared_ptr<completion_sink_t> sink)>;

/**
 * Creates a backend that delegates provider access to one caller-supplied callback.
 * @param start_callback Callback that starts one completion request.
 * @return Shared backend that forwards every request to the callback.
 */
std::shared_ptr<completion_backend_t>
create_callback_completion_backend(completion_start_callback_t start_callback);

}  // namespace qompi
