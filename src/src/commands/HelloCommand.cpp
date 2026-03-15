/*! Declares the sample `/hello` slash command. */

#include "SlashCommandRegistry.h"

namespace qcai2
{

void helloCommand(const SlashCommandInvocation &invocation, const SlashCommandContext &context)
{
    Q_UNUSED(invocation);

    if (context.logMessage)
    {
        context.logMessage(QStringLiteral("Hello World"));
    }
}

DECLARE_COMMAND(hello, "Write Hello World to the Actions Log.", helloCommand)

}  // namespace qcai2
