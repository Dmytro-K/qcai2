#include "OllamaProvider.h"

namespace Qcai2 {

OllamaProvider::OllamaProvider(QObject *parent)
    : OpenAICompatibleProvider(parent)
{
    // Default Ollama endpoint
    setBaseUrl(QStringLiteral("http://localhost:11434"));
}

} // namespace Qcai2
