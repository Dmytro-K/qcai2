/*! Declares a registry and dispatcher for dock slash commands. */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace qcai2
{

/**
 * Parsed slash command invocation from the goal editor.
 */
struct SlashCommandInvocation
{
    /** Full trimmed user input, including the leading slash. */
    QString input;

    /** Normalized command name without the leading slash. */
    QString name;

    /** Remaining text after the command name. */
    QString arguments;
};

/**
 * Services exposed to slash command handlers.
 */
struct SlashCommandContext
{
    /** Appends a user-visible entry to the Actions Log. */
    std::function<void(const QString &)> logMessage;
};

/**
 * Result of trying to dispatch a goal as a slash command.
 */
struct SlashCommandDispatchResult
{
    /** True when the input started with a slash and was treated as a command. */
    bool isSlashCommand = false;

    /** True when a registered command handler ran successfully. */
    bool executed = false;

    /** Canonical command name that matched, without the leading slash. */
    QString commandName;

    /** User-visible error to log when dispatch failed. */
    QString errorMessage;
};

/**
 * Definition record for one slash command contributed by a source file.
 */
struct SlashCommandDefinition
{
    /** Command name without the leading slash. */
    const char *name = nullptr;

    /** Short description for UI and debugging. */
    const char *description = nullptr;

    /** Handler function invoked when the command executes. */
    void (*handler)(const SlashCommandInvocation &, const SlashCommandContext &) = nullptr;
};

namespace detail
{

/**
 * Registers one statically declared slash command definition.
 * This is used by DECLARE_COMMAND and should not be called manually.
 */
void registerSlashCommandDefinition(const SlashCommandDefinition *definition);

}  // namespace detail

/**
 * Registry of dock-local slash commands such as `/hello`.
 */
class SlashCommandRegistry
{
public:
    /**
     * Display metadata for one registered slash command.
     */
    struct CommandInfo
    {
        QString name;
        QString description;
    };

    /**
     * Creates the registry from all statically declared command definitions.
     */
    SlashCommandRegistry();

    /**
     * Returns registered slash commands with names prefixed by `/`.
     */
    QList<CommandInfo> commands() const;

    /**
     * Returns registered slash command names prefixed with `/`, sorted for UI use.
     */
    QStringList commandNames() const;

    /**
     * Tries to execute a slash command from the raw goal text.
     * @param input Goal text from the dock editor.
     * @param context Services exposed to the command handler.
     */
    SlashCommandDispatchResult dispatch(const QString &input,
                                        const SlashCommandContext &context) const;

private:
    struct Entry
    {
        QString displayName;
        QString description;
        void (*handler)(const SlashCommandInvocation &, const SlashCommandContext &) = nullptr;
    };

    QHash<QString, Entry> m_commands;
};

}  // namespace qcai2

#define QCAI2_SLASH_COMMAND_JOIN_IMPL(a, b) a##b
#define QCAI2_SLASH_COMMAND_JOIN(a, b) QCAI2_SLASH_COMMAND_JOIN_IMPL(a, b)

/**
 * Declares one slash command and auto-registers it during static initialization.
 *
 * Usage:
 * void helloCommand(const qcai2::SlashCommandInvocation &, const qcai2::SlashCommandContext &);
 * DECLARE_COMMAND(hello, "Write Hello World to the Actions Log.", helloCommand)
 * {
 *     if (context.logMessage)
 *         context.logMessage(QStringLiteral("Hello World"));
 * }
 */
#define DECLARE_COMMAND(NAME, DESCRIPTION, FUNCTION)                                              \
    namespace                                                                                     \
    {                                                                                             \
    const ::qcai2::SlashCommandDefinition QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandDefinition_,  \
                                                                   NAME){#NAME, DESCRIPTION,      \
                                                                         FUNCTION};               \
    struct QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrar_, NAME)                            \
    {                                                                                             \
        QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrar_, NAME)()                             \
        {                                                                                         \
            ::qcai2::detail::registerSlashCommandDefinition(                                      \
                &QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandDefinition_, NAME));                   \
        }                                                                                         \
    };                                                                                            \
    const QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrar_, NAME)                             \
        QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrarInstance_, NAME);                      \
    }
