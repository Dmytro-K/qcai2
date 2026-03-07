#pragma once

#include "OpenAICompatibleProvider.h"

namespace qcai2
{

/**
 * Thin adapter that points the OpenAI-compatible provider at Ollama.
 */
class OllamaProvider : public OpenAICompatibleProvider
{
    Q_OBJECT
public:
    /**
     * Creates an Ollama provider with the default local endpoint.
     * @param parent Parent QObject that owns this instance.
     */
    explicit OllamaProvider(QObject *parent = nullptr);

    /**
     * Returns the provider identifier used in configuration.
     */
    QString id() const override
    {
        return QStringLiteral("ollama");
    }
    /**
     * Returns the user-visible provider name.
     */
    QString displayName() const override
    {
        return QStringLiteral("Ollama (Local)");
    }
};

}  // namespace qcai2
