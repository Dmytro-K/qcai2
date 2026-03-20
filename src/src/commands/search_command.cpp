/*! Declares the `/search` slash command — asks the AI to perform vector search for a query. */

#include "slash_command_registry.h"

#include "../vector_search/vector_search_service.h"

namespace qcai2
{

void search_command(const slash_command_invocation_t &invocation,
                    const slash_command_context_t &context)
{
    const QString query = invocation.arguments.trimmed();
    if (query.isEmpty())
    {
        if (context.log_message)
        {
            context.log_message(QStringLiteral("❌ Usage: /search <query>"));
        }
        return;
    }

    auto &service = vector_search_service();
    if (service.is_available() == false)
    {
        const QString status =
            service.last_error().isEmpty() ? service.status_message() : service.last_error();
        if (context.log_message)
        {
            context.log_message(QStringLiteral("❌ Vector search is unavailable: %1").arg(status));
        }
        return;
    }

    QString prompt =
        QStringLiteral(
            "Use the configured vector search backend to search the project for: \"%1\".")
            .arg(query);
    if (const vector_search_backend_t *backend = service.backend(); backend != nullptr)
    {
        prompt += QStringLiteral(" The active backend is `%1`.").arg(backend->display_name());
    }
    prompt += QStringLiteral(
        "\n\nReturn the most relevant matches, explain why each result is relevant, and cite file "
        "paths and snippets whenever possible.");

    if (context.goal_override != nullptr)
    {
        *context.goal_override = prompt;
    }
}

DECLARE_COMMAND(search, "Search the project with the configured vector search backend.",
                search_command)

}  // namespace qcai2
