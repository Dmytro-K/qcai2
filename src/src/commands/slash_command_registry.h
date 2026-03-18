/*! Declares a registry and dispatcher for dock slash commands. */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace qcai2
{

struct slash_command_invocation_t
{
    QString input;
    QString name;
    QString arguments;
};

struct slash_command_context_t
{
    std::function<void(const QString &)> log_message;
    QString *goal_override = nullptr;
};

struct slash_command_dispatch_result_t
{
    bool is_slash_command = false;
    bool executed = false;
    QString command_name;
    QString error_message;
    QString goal_override;
};

struct slash_command_definition_t
{
    const char *name = nullptr;
    const char *description = nullptr;
    void (*handler)(const slash_command_invocation_t &, const slash_command_context_t &) = nullptr;
};

namespace detail
{

void register_slash_command_definition(const slash_command_definition_t *definition);

}  // namespace detail

class slash_command_registry_t
{
public:
    struct command_info_t
    {
        QString name;
        QString description;
    };

    slash_command_registry_t();

    QList<command_info_t> commands() const;
    QStringList command_names() const;
    slash_command_dispatch_result_t dispatch(const QString &input,
                                             const slash_command_context_t &context) const;

private:
    struct entry_t
    {
        QString display_name;
        QString description;
        void (*handler)(const slash_command_invocation_t &,
                        const slash_command_context_t &) = nullptr;
    };

    QHash<QString, entry_t> registered_commands;
};

}  // namespace qcai2

#define QCAI2_SLASH_COMMAND_JOIN_IMPL(a, b) a##b
#define QCAI2_SLASH_COMMAND_JOIN(a, b) QCAI2_SLASH_COMMAND_JOIN_IMPL(a, b)

#define DECLARE_COMMAND(NAME, DESCRIPTION, FUNCTION)                                              \
    namespace                                                                                     \
    {                                                                                             \
    const ::qcai2::slash_command_definition_t                                                     \
        QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandDefinition_, NAME){#NAME, DESCRIPTION,          \
                                                                     FUNCTION};                   \
    struct QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrar_, NAME)                            \
    {                                                                                             \
        QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrar_, NAME)()                             \
        {                                                                                         \
            ::qcai2::detail::register_slash_command_definition(                                   \
                &QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandDefinition_, NAME));                   \
        }                                                                                         \
    };                                                                                            \
    const QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrar_, NAME)                             \
        QCAI2_SLASH_COMMAND_JOIN(qcai2SlashCommandRegistrarInstance_, NAME);                      \
    }
