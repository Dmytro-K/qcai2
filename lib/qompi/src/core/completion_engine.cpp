/*! @file
    @brief Implements the default completion engine factory.
*/

#include "qompi/completion_engine.h"

#include "qompi/completion_backend.h"
#include "qompi/completion_session.h"

#include <utility>

namespace qompi
{

namespace
{

/**
 * Default engine implementation that creates latest-wins sessions for one backend.
 */
class completion_engine_impl_t final : public completion_engine_t
{
public:
    /**
     * Creates the engine.
     * @param backend Shared backend used for created sessions.
     */
    explicit completion_engine_impl_t(std::shared_ptr<completion_backend_t> backend)
        : backend(std::move(backend))
    {
    }

    /** @copydoc completion_engine_t::create_session */
    std::shared_ptr<completion_session_t> create_session() override
    {
        return create_latest_wins_completion_session(this->backend);
    }

private:
    std::shared_ptr<completion_backend_t> backend;
};

}  // namespace

std::unique_ptr<completion_engine_t>
create_completion_engine(std::shared_ptr<completion_backend_t> backend)
{
    return std::make_unique<completion_engine_impl_t>(std::move(backend));
}

}  // namespace qompi
