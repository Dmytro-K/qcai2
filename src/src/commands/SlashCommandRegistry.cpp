/*! Implements the dock slash-command registry. */

#include "SlashCommandRegistry.h"

#include <algorithm>

namespace qcai2
{

namespace
{

QString normalizedCommandName(QString name)
{
    name = name.trimmed();
    if (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    return name.toLower();
}

SlashCommandInvocation parseSlashCommand(const QString &input)
{
    const QString trimmedInput = input.trimmed();
    const QString body = trimmedInput.mid(1).trimmed();

    qsizetype nameEnd = 0;
    while (nameEnd < body.size() && !body.at(nameEnd).isSpace())
        ++nameEnd;

    SlashCommandInvocation invocation;
    invocation.input = trimmedInput;
    invocation.name = normalizedCommandName(body.left(nameEnd));
    invocation.arguments = body.mid(nameEnd).trimmed();
    return invocation;
}

}  // namespace

namespace detail
{

QList<const SlashCommandDefinition *> &registeredSlashCommandDefinitions()
{
    static QList<const SlashCommandDefinition *> definitions;
    return definitions;
}

void registerSlashCommandDefinition(const SlashCommandDefinition *definition)
{
    if (definition == nullptr)
        return;

    registeredSlashCommandDefinitions().append(definition);
}

}  // namespace detail

SlashCommandRegistry::SlashCommandRegistry()
{
    const auto &definitions = detail::registeredSlashCommandDefinitions();
    for (const SlashCommandDefinition *definition : definitions)
    {
        if (definition == nullptr || definition->handler == nullptr)
            continue;

        const QString key = normalizedCommandName(QString::fromUtf8(definition->name));
        if (key.isEmpty())
            continue;

        Entry entry;
        entry.displayName = key;
        entry.description = QString::fromUtf8(definition->description).trimmed();
        entry.handler = definition->handler;
        m_commands.insert(key, std::move(entry));
    }
}

QList<SlashCommandRegistry::CommandInfo> SlashCommandRegistry::commands() const
{
    QList<CommandInfo> commandInfos;
    commandInfos.reserve(m_commands.size());

    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it)
        commandInfos.append({QStringLiteral("/%1").arg(it->displayName), it->description});

    std::sort(commandInfos.begin(), commandInfos.end(),
              [](const CommandInfo &lhs, const CommandInfo &rhs) {
                  return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
              });
    return commandInfos;
}

QStringList SlashCommandRegistry::commandNames() const
{
    QStringList names;
    const QList<CommandInfo> commandInfos = commands();
    names.reserve(commandInfos.size());
    for (const CommandInfo &command : commandInfos)
        names.append(command.name);
    return names;
}

SlashCommandDispatchResult SlashCommandRegistry::dispatch(const QString &input,
                                                          const SlashCommandContext &context) const
{
    const QString trimmedInput = input.trimmed();
    if (!trimmedInput.startsWith(QLatin1Char('/')))
        return {};

    const SlashCommandInvocation invocation = parseSlashCommand(trimmedInput);
    SlashCommandDispatchResult result;
    result.isSlashCommand = true;
    result.commandName = invocation.name;

    if (invocation.name.isEmpty())
    {
        result.errorMessage = QStringLiteral("❌ Slash command is empty.");
        return result;
    }

    const auto it = m_commands.constFind(invocation.name);
    if (it == m_commands.cend())
    {
        result.errorMessage = QStringLiteral("❌ Unknown slash command: /%1").arg(invocation.name);
        return result;
    }

    result.commandName = it->displayName;
    it->handler(invocation, context);
    result.executed = true;
    return result;
}

}  // namespace qcai2
