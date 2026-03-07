#pragma once

#include "OpenAICompatibleProvider.h"

namespace qcai2
{

// Adapter for Ollama (http://localhost:11434).
// Ollama supports the OpenAI-compatible /v1/chat/completions endpoint.
class OllamaProvider : public OpenAICompatibleProvider
{
    Q_OBJECT
public:
    explicit OllamaProvider(QObject *parent = nullptr);

    QString id() const override
    {
        return QStringLiteral("ollama");
    }
    QString displayName() const override
    {
        return QStringLiteral("Ollama (Local)");
    }
};

}  // namespace qcai2
