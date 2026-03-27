/*! @file
    @brief Implements the generic callback-backed completion backend.
*/

#include "qompi/callback_completion_backend.h"

#include <stdexcept>
#include <utility>

namespace qompi
{

namespace
{

/**
 * Callback-backed backend implementation used by plugin integration layers.
 */
class callback_completion_backend_t final : public completion_backend_t
{
public:
    /**
     * Creates the backend.
     * @param start_callback Callback used to start one request.
     */
    explicit callback_completion_backend_t(completion_start_callback_t start_callback)
        : start_callback(std::move(start_callback))
    {
    }

    /** @copydoc completion_backend_t::start_completion */
    std::shared_ptr<completion_operation_t>
    start_completion(const completion_request_t &request, const completion_options_t &options,
                     std::shared_ptr<completion_sink_t> sink) override
    {
        return this->start_callback(request, options, std::move(sink));
    }

private:
    completion_start_callback_t start_callback;
};

}  // namespace

std::shared_ptr<completion_backend_t>
create_callback_completion_backend(completion_start_callback_t start_callback)
{
    if (static_cast<bool>(start_callback) == false)
    {
        throw std::invalid_argument("qompi callback backend requires a start callback");
    }
    return std::make_shared<callback_completion_backend_t>(std::move(start_callback));
}

}  // namespace qompi
