#include "OllamaProvider.h"

namespace qcai2
{

/**
 * Creates an Ollama provider pointed at the default localhost endpoint.
 * @param parent Parent QObject that owns this instance.
 */
OllamaProvider::OllamaProvider(QObject *parent) : OpenAICompatibleProvider(parent)
{
    // Default Ollama endpoint
    setBaseUrl(QStringLiteral("http://localhost:11434"));
}

}  // namespace qcai2
