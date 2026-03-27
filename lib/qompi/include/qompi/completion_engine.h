/*! @file
    @brief Declares the top-level factory that creates qompi completion sessions.
*/

#pragma once

#include <memory>

namespace qompi
{

class completion_backend_t;
class completion_session_t;

/**
 * Creates per-caller completion sessions backed by one shared provider implementation.
 */
class completion_engine_t
{
public:
    /** Virtual destructor for polymorphic use. */
    virtual ~completion_engine_t() = default;

    /**
     * Creates a new completion session.
     * @return Shared session instance.
     */
    virtual std::shared_ptr<completion_session_t> create_session() = 0;
};

/**
 * Creates the default completion engine implementation for one backend.
 * @param backend Backend used for future sessions.
 * @return Newly created engine.
 */
std::unique_ptr<completion_engine_t>
create_completion_engine(std::shared_ptr<completion_backend_t> backend);

}  // namespace qompi
