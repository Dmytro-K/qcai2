/*! @file
    @brief Unit tests for dock slash-command dispatch.
*/

#include <QtTest>

#include "src/commands/slash_command_registry.h"
#include "src/goal/file_reference_goal_handler.h"
#include "src/goal/slash_command_goal_handler.h"

using namespace qcai2;

/**
 * @brief Verifies parsing and dispatch of dock-local slash commands.
 */
class slash_command_registry_test_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Exposes registered commands as `/`-prefixed completion items.
     */
    void command_names_returns_slash_prefixed_names();

    /**
     * @brief Leaves regular prompts untouched so they still go to the agent.
     */
    void dispatch_non_slash_input_is_ignored();

    /**
     * @brief Reports an actionable error when the slash command is not registered.
     */
    void dispatch_unknown_command_reports_error();

    /**
     * @brief Executes `/hello` and writes its output through the log callback.
     */
    void dispatch_hello_logs_hello_world();

    /**
     * @brief Allows test-only commands to auto-register through DECLARE_COMMAND.
     */
    void dispatch_macro_declared_command_is_registered();

    /**
     * @brief Offers popup completions for slash commands at the beginning of the goal.
     */
    void completion_session_leading_slash_returns_matching_commands();

    /**
     * @brief Does not activate slash completions when `/` appears later in the goal.
     */
    void completion_session_non_leading_slash_is_ignored();

    /**
     * @brief Colors the leading slash command token with the link color.
     */
    void highlight_spans_leading_slash_command_is_blue();

    /**
     * @brief Offers popup completions for # file references.
     */
    void file_completion_hash_prefix_returns_matching_files();

    /**
     * @brief Orders case-sensitive # file matches before case-insensitive ones.
     */
    void file_completion_case_sensitive_matches_are_ordered_first();

    /**
     * @brief Highlights # file references with a dedicated color.
     */
    void file_highlight_hash_reference_is_colored();
};

void macro_test_command(const slash_command_invocation_t &invocation,
                        const slash_command_context_t &context)
{
    Q_UNUSED(invocation);

    if (context.log_message)
    {
        context.log_message(QStringLiteral("Macro test"));
    }
}

DECLARE_COMMAND(macro_test, "Auto-registered test command.", macro_test_command)

void slash_command_registry_test_t::command_names_returns_slash_prefixed_names()
{
    slash_command_registry_t registry;

    QCOMPARE(registry.command_names(),
             QStringList({QStringLiteral("/hello"), QStringLiteral("/macro_test")}));
}

void slash_command_registry_test_t::dispatch_non_slash_input_is_ignored()
{
    slash_command_registry_t registry;

    const slash_command_dispatch_result_t result =
        registry.dispatch(QStringLiteral("explain this file"), slash_command_context_t{});

    QVERIFY(!result.is_slash_command);
    QVERIFY(!result.executed);
    QVERIFY(result.error_message.isEmpty());
}

void slash_command_registry_test_t::dispatch_unknown_command_reports_error()
{
    slash_command_registry_t registry;

    const slash_command_dispatch_result_t result =
        registry.dispatch(QStringLiteral("/missing something"), slash_command_context_t{});

    QVERIFY(result.is_slash_command);
    QVERIFY(!result.executed);
    QCOMPARE(result.command_name, QStringLiteral("missing"));
    QCOMPARE(result.error_message, QStringLiteral("❌ Unknown slash command: /missing"));
}

void slash_command_registry_test_t::dispatch_hello_logs_hello_world()
{
    slash_command_registry_t registry;
    QStringList log_entries;

    const slash_command_dispatch_result_t result = registry.dispatch(
        QStringLiteral("/hello"), slash_command_context_t{[&log_entries](const QString &message) {
            log_entries.append(message);
        }});

    QVERIFY(result.is_slash_command);
    QVERIFY(result.executed);
    QCOMPARE(result.command_name, QStringLiteral("hello"));
    QCOMPARE(log_entries, QStringList{QStringLiteral("Hello World")});
}

void slash_command_registry_test_t::dispatch_macro_declared_command_is_registered()
{
    slash_command_registry_t registry;
    QStringList log_entries;

    const slash_command_dispatch_result_t result =
        registry.dispatch(QStringLiteral("/macro_test"),
                          slash_command_context_t{[&log_entries](const QString &message) {
                              log_entries.append(message);
                          }});

    QVERIFY(result.is_slash_command);
    QVERIFY(result.executed);
    QCOMPARE(result.command_name, QStringLiteral("macro_test"));
    QCOMPARE(log_entries, QStringList{QStringLiteral("Macro test")});
}

void slash_command_registry_test_t::completion_session_leading_slash_returns_matching_commands()
{
    slash_command_registry_t registry;
    slash_command_goal_handler_t handler(&registry);

    const goal_completion_session_t session =
        handler.completion_session({QStringLiteral("   /he arg"), 6});

    QVERIFY(session.active);
    QCOMPARE(session.replace_start, 3);
    QCOMPARE(session.replace_end, 6);
    QCOMPARE(session.prefix, QStringLiteral("/he"));
    QCOMPARE(session.items.size(), 1);
    QCOMPARE(session.items.first().label, QStringLiteral("/hello"));
    QCOMPARE(session.items.first().insert_text, QStringLiteral("/hello"));
}

void slash_command_registry_test_t::completion_session_non_leading_slash_is_ignored()
{
    slash_command_registry_t registry;
    slash_command_goal_handler_t handler(&registry);

    const goal_completion_session_t session =
        handler.completion_session({QStringLiteral("say /he"), 7});

    QVERIFY(!session.active);
    QVERIFY(session.items.isEmpty());
}

void slash_command_registry_test_t::highlight_spans_leading_slash_command_is_blue()
{
    slash_command_registry_t registry;
    slash_command_goal_handler_t handler(&registry);
    const QPalette palette;

    const QList<goal_highlight_span_t> spans =
        handler.highlight_spans(QStringLiteral("  /hello world"), palette);

    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().start, 2);
    QCOMPARE(spans.first().length, 6);
    QCOMPARE(spans.first().format.foreground().color(), palette.color(QPalette::Link));
}

void slash_command_registry_test_t::file_completion_hash_prefix_returns_matching_files()
{
    file_reference_goal_handler_t handler(
        []() { return QStringList{QStringLiteral("src/main.cpp"), QStringLiteral("README.md")}; });

    const goal_completion_session_t session =
        handler.completion_session({QStringLiteral("Check #sr now"), 9});

    QVERIFY(session.active);
    QCOMPARE(session.replace_start, 6);
    QCOMPARE(session.replace_end, 9);
    QCOMPARE(session.prefix, QStringLiteral("#sr"));
    QCOMPARE(session.items.size(), 1);
    QCOMPARE(session.items.first().label, QStringLiteral("#src/main.cpp"));
    QCOMPARE(session.items.first().insert_text, QStringLiteral("#src/main.cpp"));
}

void slash_command_registry_test_t::file_completion_case_sensitive_matches_are_ordered_first()
{
    file_reference_goal_handler_t handler([]() {
        return QStringList{QStringLiteral("src/Main.cpp"), QStringLiteral("Src/main.cpp"),
                           QStringLiteral("src/main.cpp")};
    });

    const goal_completion_session_t session =
        handler.completion_session({QStringLiteral("Check #Ma now"), 9});

    QVERIFY(session.active);
    QCOMPARE(session.items.size(), 3);
    QCOMPARE(session.items.at(0).label, QStringLiteral("#src/Main.cpp"));
    QCOMPARE(session.items.at(1).label, QStringLiteral("#Src/main.cpp"));
    QCOMPARE(session.items.at(2).label, QStringLiteral("#src/main.cpp"));
}

void slash_command_registry_test_t::file_highlight_hash_reference_is_colored()
{
    file_reference_goal_handler_t handler([]() { return QStringList{}; });
    const QPalette palette;

    const QList<goal_highlight_span_t> spans =
        handler.highlight_spans(QStringLiteral("Use #src/main.cpp please"), palette);

    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().start, 4);
    QCOMPARE(spans.first().length, 13);
    QVERIFY(spans.first().format.foreground().color().isValid());
}

QTEST_APPLESS_MAIN(slash_command_registry_test_t)

#include "tst_slash_commands.moc"
