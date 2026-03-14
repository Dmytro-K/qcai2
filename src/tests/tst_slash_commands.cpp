/*! @file
    @brief Unit tests for dock slash-command dispatch.
*/

#include <QtTest>

#include "src/commands/SlashCommandRegistry.h"

using namespace qcai2;

/**
 * @brief Verifies parsing and dispatch of dock-local slash commands.
 */
class SlashCommandRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Exposes registered commands as `/`-prefixed completion items.
     */
    void commandNames_returnsSlashPrefixedNames();

    /**
     * @brief Leaves regular prompts untouched so they still go to the agent.
     */
    void dispatch_nonSlashInput_isIgnored();

    /**
     * @brief Reports an actionable error when the slash command is not registered.
     */
    void dispatch_unknownCommand_reportsError();

    /**
     * @brief Executes `/hello` and writes its output through the log callback.
     */
    void dispatch_hello_logsHelloWorld();

    /**
     * @brief Allows test-only commands to auto-register through DECLARE_COMMAND.
     */
    void dispatch_macroDeclaredCommand_isRegistered();
};

void macroTestCommand(const SlashCommandInvocation &invocation, const SlashCommandContext &context)
{
    Q_UNUSED(invocation);

    if (context.logMessage)
        context.logMessage(QStringLiteral("Macro test"));
}

DECLARE_COMMAND(macro_test, "Auto-registered test command.", macroTestCommand)

void SlashCommandRegistryTest::commandNames_returnsSlashPrefixedNames()
{
    SlashCommandRegistry registry;

    QCOMPARE(registry.commandNames(),
             QStringList({QStringLiteral("/hello"), QStringLiteral("/macro_test")}));
}

void SlashCommandRegistryTest::dispatch_nonSlashInput_isIgnored()
{
    SlashCommandRegistry registry;

    const SlashCommandDispatchResult result = registry.dispatch(
        QStringLiteral("explain this file"), SlashCommandContext{});

    QVERIFY(!result.isSlashCommand);
    QVERIFY(!result.executed);
    QVERIFY(result.errorMessage.isEmpty());
}

void SlashCommandRegistryTest::dispatch_unknownCommand_reportsError()
{
    SlashCommandRegistry registry;

    const SlashCommandDispatchResult result =
        registry.dispatch(QStringLiteral("/missing something"), SlashCommandContext{});

    QVERIFY(result.isSlashCommand);
    QVERIFY(!result.executed);
    QCOMPARE(result.commandName, QStringLiteral("missing"));
    QCOMPARE(result.errorMessage, QStringLiteral("❌ Unknown slash command: /missing"));
}

void SlashCommandRegistryTest::dispatch_hello_logsHelloWorld()
{
    SlashCommandRegistry registry;
    QStringList logEntries;

    const SlashCommandDispatchResult result = registry.dispatch(
        QStringLiteral("/hello"),
        SlashCommandContext{[&logEntries](const QString &message) { logEntries.append(message); }});

    QVERIFY(result.isSlashCommand);
    QVERIFY(result.executed);
    QCOMPARE(result.commandName, QStringLiteral("hello"));
    QCOMPARE(logEntries, QStringList{QStringLiteral("Hello World")});
}

void SlashCommandRegistryTest::dispatch_macroDeclaredCommand_isRegistered()
{
    SlashCommandRegistry registry;
    QStringList logEntries;

    const SlashCommandDispatchResult result = registry.dispatch(
        QStringLiteral("/macro_test"),
        SlashCommandContext{[&logEntries](const QString &message) { logEntries.append(message); }});

    QVERIFY(result.isSlashCommand);
    QVERIFY(result.executed);
    QCOMPARE(result.commandName, QStringLiteral("macro_test"));
    QCOMPARE(logEntries, QStringList{QStringLiteral("Macro test")});
}

QTEST_APPLESS_MAIN(SlashCommandRegistryTest)

#include "tst_slash_commands.moc"
