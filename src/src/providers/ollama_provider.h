#pragma once

#include "open_ai_compatible_provider.h"

namespace qcai2
{

/**
 * Thin adapter that points the OpenAI-compatible provider at Ollama.
 */
class ollama_provider_t : public open_ai_compatible_provider_t
{
    Q_OBJECT
public:
    /**
     * Creates an Ollama provider with the default local endpoint.
     * @param parent Parent QObject that owns this instance.
     */
    explicit ollama_provider_t(QObject *parent = nullptr);

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
    QString display_name() const override
    {
        return QStringLiteral("Ollama (Local)");
    }
};

}  // namespace qcai2
