/*! @file
    @brief Covers effective completion request settings resolution.
*/

#include "../src/completion/completion_request_settings.h"

#include <QtTest>

namespace qcai2
{

class tst_completion_request_settings_t : public QObject
{
    Q_OBJECT

private slots:
    void defaults_follow_agent_settings();
    void dedicated_provider_uses_provider_default_model();
    void thinking_instruction_requires_opt_in();
    void reasoning_can_be_disabled();
};

void tst_completion_request_settings_t::defaults_follow_agent_settings()
{
    settings_t settings;
    settings.provider = QStringLiteral("openai");
    settings.model_name = QStringLiteral("gpt-5.4");

    const completion_request_settings_t resolved = resolve_completion_request_settings(settings);

    QCOMPARE(resolved.provider_id, QStringLiteral("openai"));
    QCOMPARE(resolved.model, QStringLiteral("gpt-5.4"));
    QCOMPARE(resolved.selected_reasoning_effort, QStringLiteral("off"));
    QCOMPARE(completion_reasoning_effort_to_send(resolved), QStringLiteral("off"));
    QCOMPARE(completion_thinking_instruction(resolved), QString());
}

void tst_completion_request_settings_t::dedicated_provider_uses_provider_default_model()
{
    settings_t settings;
    settings.provider = QStringLiteral("openai");
    settings.model_name = QStringLiteral("gpt-5.4");
    settings.completion_provider = QStringLiteral("copilot");
    settings.copilot_model = QStringLiteral("gpt-5.4-mini");

    const completion_request_settings_t resolved = resolve_completion_request_settings(settings);

    QCOMPARE(resolved.provider_id, QStringLiteral("copilot"));
    QCOMPARE(resolved.model, QStringLiteral("gpt-5.4-mini"));
}

void tst_completion_request_settings_t::thinking_instruction_requires_opt_in()
{
    settings_t settings;
    settings.completion_send_thinking = true;
    settings.completion_thinking_level = QStringLiteral("medium");

    const completion_request_settings_t resolved = resolve_completion_request_settings(settings);

    QCOMPARE(completion_thinking_instruction(resolved),
             QStringLiteral("Use medium thinking depth for this task."));
}

void tst_completion_request_settings_t::reasoning_can_be_disabled()
{
    settings_t settings;
    settings.completion_reasoning_effort = QStringLiteral("high");
    settings.completion_send_reasoning = false;

    const completion_request_settings_t resolved = resolve_completion_request_settings(settings);

    QCOMPARE(resolved.selected_reasoning_effort, QStringLiteral("high"));
    QCOMPARE(completion_reasoning_effort_to_send(resolved), QString());
}

}  // namespace qcai2

QTEST_GUILESS_MAIN(qcai2::tst_completion_request_settings_t)

#include "tst_completion_request_settings.moc"
