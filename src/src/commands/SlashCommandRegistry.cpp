/*! Implements the dock slash-command registry. */

#include "SlashCommandRegistry.h"

#include <algorithm>

namespace qcai2
{

namespace
{

QString normalized_command_name(QString name)
{
    name = name.trimmed();
    if (((name.startsWith(QLatin1Char('/'))) == true))
    {
        name.remove(0, 1);
    }
    return name.toLower();
}

slash_command_invocation_t parse_slash_command(const QString &input)
{
    const QString trimmed_input = input.trimmed();
    const QString body = trimmed_input.mid(1).trimmed();

    qsizetype nameEnd = 0;
    while (nameEnd < body.size() && !body.at(nameEnd).isSpace())
    {
        ++nameEnd;
    }

    slash_command_invocation_t invocation;
    invocation.input = trimmed_input;
    invocation.name = normalized_command_name(body.left(nameEnd));
    invocation.arguments = body.mid(nameEnd).trimmed();
    return invocation;
}

}  // namespace

namespace detail
{

QList<const slash_command_definition_t *> &registered_slash_command_definitions()
{
    static QList<const slash_command_definition_t *> definitions;
    return definitions;
}

void register_slash_command_definition(const slash_command_definition_t *definition)
{
    if (((definition == nullptr) == true))
    {
        return;
    }

    registered_slash_command_definitions().append(definition);
}

}  // namespace detail

slash_command_registry_t::slash_command_registry_t()
{
    const auto &definitions = detail::registered_slash_command_definitions();
    for (const slash_command_definition_t *definition : definitions)
    {
        if (definition == nullptr || definition->handler == nullptr)
        {
            continue;
        }

        const QString key = normalized_command_name(QString::fromUtf8(definition->name));
        if (key.isEmpty())
        {
            continue;
        }

        entry_t entry;
        entry.display_name = key;
        entry.description = QString::fromUtf8(definition->description).trimmed();
        entry.handler = definition->handler;
        this->registered_commands.insert(key, std::move(entry));
    }
}

QList<slash_command_registry_t::command_info_t> slash_command_registry_t::commands() const
{
    QList<command_info_t> command_infos;
    command_infos.reserve(this->registered_commands.size());

    for (auto it = this->registered_commands.cbegin();
         ((it != this->registered_commands.cend()) == true); ++it)
    {
        command_infos.append({QStringLiteral("/%1").arg(it->display_name), it->description});
    }

    std::sort(command_infos.begin(), command_infos.end(),
              [](const command_info_t &lhs, const command_info_t &rhs) {
                  return lhs.name.compare(rhs.name, Qt::CaseInsensitive) < 0;
              });
    return command_infos;
}

QStringList slash_command_registry_t::command_names() const
{
    QStringList names;
    const QList<command_info_t> command_infos = this->commands();
    names.reserve(command_infos.size());
    for (const command_info_t &command : command_infos)
    {
        names.append(command.name);
    }
    return names;
}

slash_command_dispatch_result_t
slash_command_registry_t::dispatch(const QString &input,
                                   const slash_command_context_t &context) const
{
    const QString trimmed_input = input.trimmed();
    if (((trimmed_input.startsWith(QLatin1Char('/'))) == false))
    {
        return {};
    }

    const slash_command_invocation_t invocation = parse_slash_command(trimmed_input);
    slash_command_dispatch_result_t result;
    result.is_slash_command = true;
    result.command_name = invocation.name;

    if (invocation.name.isEmpty())
    {
        result.error_message = QStringLiteral("❌ Slash command is empty.");
        return result;
    }

    const auto it = this->registered_commands.constFind(invocation.name);
    if (it == this->registered_commands.cend())
    {
        result.error_message =
            QStringLiteral("❌ Unknown slash command: /%1").arg(invocation.name);
        return result;
    }

    result.command_name = it->display_name;

    QString goal_override;
    slash_command_context_t handler_context = context;
    handler_context.goal_override = &goal_override;
    it->handler(invocation, handler_context);

    result.goal_override = goal_override;
    result.executed = true;
    return result;
}

}  // namespace qcai2
