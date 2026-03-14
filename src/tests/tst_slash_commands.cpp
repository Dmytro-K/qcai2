/*! @file
    @brief Unit tests for dock slash-command dispatch.
*/

#include <QtTest>

#include "src/commands/SlashCommandRegistry.h"
#include "src/goal/FileReferenceGoalHandler.h"
#include "src/goal/SlashCommandGoalHandler.h"

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

    /**
     * @brief Offers popup completions for slash commands at the beginning of the goal.
     */
    void completionSession_leadingSlash_returnsMatchingCommands();

    /**
     * @brief Does not activate slash completions when `/` appears later in the goal.
     */
    void completionSession_nonLeadingSlash_isIgnored();

    /**
     * @brief Colors the leading slash command token with the link color.
     */
    void highlightSpans_leadingSlashCommand_isBlue();

    /**
     * @brief Offers popup completions for # file references.
     */
    void fileCompletion_hashPrefix_returnsMatchingFiles();

    /**
     * @brief Orders case-sensitive # file matches before case-insensitive ones.
     */
    void fileCompletion_caseSensitiveMatches_areOrderedFirst();

    /**
     * @brief Highlights # file references with a dedicated color.
     */
    void fileHighlight_hashReference_isColored();
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

void SlashCommandRegistryTest::completionSession_leadingSlash_returnsMatchingCommands()
{
    SlashCommandRegistry registry;
    SlashCommandGoalHandler handler(&registry);

    const GoalCompletionSession session =
        handler.completionSession({QStringLiteral("   /he arg"), 6});

    QVERIFY(session.active);
    QCOMPARE(session.replaceStart, 3);
    QCOMPARE(session.replaceEnd, 6);
    QCOMPARE(session.prefix, QStringLiteral("/he"));
    QCOMPARE(session.items.size(), 1);
    QCOMPARE(session.items.first().label, QStringLiteral("/hello"));
    QCOMPARE(session.items.first().insertText, QStringLiteral("/hello"));
}

void SlashCommandRegistryTest::completionSession_nonLeadingSlash_isIgnored()
{
    SlashCommandRegistry registry;
    SlashCommandGoalHandler handler(&registry);

    const GoalCompletionSession session =
        handler.completionSession({QStringLiteral("say /he"), 7});

    QVERIFY(!session.active);
    QVERIFY(session.items.isEmpty());
}

void SlashCommandRegistryTest::highlightSpans_leadingSlashCommand_isBlue()
{
    SlashCommandRegistry registry;
    SlashCommandGoalHandler handler(&registry);
    const QPalette palette;

    const QList<GoalHighlightSpan> spans =
        handler.highlightSpans(QStringLiteral("  /hello world"), palette);

    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().start, 2);
    QCOMPARE(spans.first().length, 6);
    QCOMPARE(spans.first().format.foreground().color(), palette.color(QPalette::Link));
}

void SlashCommandRegistryTest::fileCompletion_hashPrefix_returnsMatchingFiles()
{
    FileReferenceGoalHandler handler(
        []() { return QStringList{QStringLiteral("src/main.cpp"), QStringLiteral("README.md")}; });

    const GoalCompletionSession session =
        handler.completionSession({QStringLiteral("Check #sr now"), 9});

    QVERIFY(session.active);
    QCOMPARE(session.replaceStart, 6);
    QCOMPARE(session.replaceEnd, 9);
    QCOMPARE(session.prefix, QStringLiteral("#sr"));
    QCOMPARE(session.items.size(), 1);
    QCOMPARE(session.items.first().label, QStringLiteral("#src/main.cpp"));
    QCOMPARE(session.items.first().insertText, QStringLiteral("#src/main.cpp"));
}

void SlashCommandRegistryTest::fileCompletion_caseSensitiveMatches_areOrderedFirst()
{
    FileReferenceGoalHandler handler([]() {
        return QStringList{QStringLiteral("src/Main.cpp"), QStringLiteral("Src/main.cpp"),
                           QStringLiteral("src/main.cpp")};
    });

    const GoalCompletionSession session =
        handler.completionSession({QStringLiteral("Check #Ma now"), 9});

    QVERIFY(session.active);
    QCOMPARE(session.items.size(), 3);
    QCOMPARE(session.items.at(0).label, QStringLiteral("#src/Main.cpp"));
    QCOMPARE(session.items.at(1).label, QStringLiteral("#Src/main.cpp"));
    QCOMPARE(session.items.at(2).label, QStringLiteral("#src/main.cpp"));
}

void SlashCommandRegistryTest::fileHighlight_hashReference_isColored()
{
    FileReferenceGoalHandler handler([]() { return QStringList{}; });
    const QPalette palette;

    const QList<GoalHighlightSpan> spans =
        handler.highlightSpans(QStringLiteral("Use #src/main.cpp please"), palette);

    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().start, 4);
    QCOMPARE(spans.first().length, 13);
    QVERIFY(spans.first().format.foreground().color().isValid());
}

QTEST_APPLESS_MAIN(SlashCommandRegistryTest)

#include "tst_slash_commands.moc"
