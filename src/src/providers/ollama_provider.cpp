#include "ollama_provider.h"

namespace qcai2
{

/**
 * Creates an Ollama provider pointed at the default localhost endpoint.
 * @param parent Parent QObject that owns this instance.
 */
ollama_provider_t::ollama_provider_t(QObject *parent) : open_ai_compatible_provider_t(parent)
{
    // Default Ollama endpoint
    set_base_url(QStringLiteral("http://localhost:11434"));
}

}  // namespace qcai2
