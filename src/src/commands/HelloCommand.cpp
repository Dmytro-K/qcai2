/*! Declares the sample `/hello` slash command. */

#include "SlashCommandRegistry.h"

namespace qcai2
{

void hello_command(const slash_command_invocation_t &invocation,
                   const slash_command_context_t &context)
{
    Q_UNUSED(invocation);

    if (context.log_message)
    {
        context.log_message(QStringLiteral("Hello World"));
    }
}

DECLARE_COMMAND(hello, "Write Hello World to the Actions Log.", hello_command)

}  // namespace qcai2
