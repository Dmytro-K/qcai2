/*! @file
    @brief Declares the qcai2 provider-backed qompi completion backend adapter.
*/

#pragma once

#include "../providers/iai_provider.h"

#include <qompi/completion_backend.h>
#include <qompi/completion_profiles.h>

#include <memory>

namespace qcai2
{

/**
 * Creates one qompi backend that delegates completion work to an existing qcai2 provider.
 * @param provider Non-owning provider used for request execution.
 * @return Backend adapter instance.
 */
std::shared_ptr<qompi::completion_backend_t>
create_qompi_provider_backend(iai_provider_t *provider);

/**
 * Creates one transport-agnostic qompi provider profile for an existing qcai2 provider.
 * @param provider Non-owning provider used at runtime.
 * @param default_model Preferred default model for the profile.
 * @return Profile metadata consumed by qompi config resolution.
 */
qompi::provider_profile_t create_qompi_provider_profile(iai_provider_t *provider,
                                                        const QString &default_model);

}  // namespace qcai2
