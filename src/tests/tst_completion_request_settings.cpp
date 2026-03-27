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
    void qompi_engine_uses_qompi_profile();
    void thinking_instruction_requires_opt_in();
    void reasoning_can_be_disabled();
    void completion_connection_settings_fall_back_to_global_provider_settings();
    void completion_connection_settings_use_completion_overrides();
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

void tst_completion_request_settings_t::qompi_engine_uses_qompi_profile()
{
    settings_t settings;
    settings.provider = QStringLiteral("openai");
    settings.model_name = QStringLiteral("gpt-5.4");
    settings.completion_engine = QStringLiteral("qompi");
    settings.qompi_completion_provider = QStringLiteral("copilot");
    settings.copilot_model = QStringLiteral("gpt-5.4-mini");
    settings.qompi_completion_send_thinking = true;
    settings.qompi_completion_thinking_level = QStringLiteral("high");
    settings.qompi_completion_send_reasoning = false;
    settings.qompi_completion_reasoning_effort = QStringLiteral("medium");

    const completion_request_settings_t resolved = resolve_completion_request_settings(settings);

    QCOMPARE(resolved.engine_kind, completion_engine_kind_t::QOMPI);
    QCOMPARE(resolved.provider_id, QStringLiteral("copilot"));
    QCOMPARE(resolved.model, QStringLiteral("gpt-5.4-mini"));
    QCOMPARE(resolved.selected_thinking_level, QStringLiteral("high"));
    QCOMPARE(completion_thinking_instruction(resolved),
             QStringLiteral("Use high thinking depth for this task."));
    QCOMPARE(completion_reasoning_effort_to_send(resolved), QString());
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

void tst_completion_request_settings_t::
    completion_connection_settings_fall_back_to_global_provider_settings()
{
    settings_t settings;
    settings.provider = QStringLiteral("openai");
    settings.base_url = QStringLiteral("https://api.example.com");
    settings.api_key = QStringLiteral("global-key");
    settings.local_base_url = QStringLiteral("http://local.example");
    settings.local_endpoint_path = QStringLiteral("/v1/completions");
    settings.local_custom_headers = QStringLiteral("X-Test: one");
    settings.ollama_base_url = QStringLiteral("http://ollama.example");
    settings.copilot_node_path = QStringLiteral("/usr/bin/node");
    settings.copilot_sidecar_path = QStringLiteral("/tmp/sidecar.js");

    const completion_connection_settings_t resolved =
        resolve_completion_connection_settings(settings);

    QCOMPARE(resolved.provider_id, QStringLiteral("openai"));
    QCOMPARE(resolved.base_url, QStringLiteral("https://api.example.com"));
    QCOMPARE(resolved.api_key, QStringLiteral("global-key"));
    QCOMPARE(resolved.local_base_url, QStringLiteral("http://local.example"));
    QCOMPARE(resolved.local_endpoint_path, QStringLiteral("/v1/completions"));
    QCOMPARE(resolved.local_custom_headers, QStringLiteral("X-Test: one"));
    QCOMPARE(resolved.ollama_base_url, QStringLiteral("http://ollama.example"));
    QCOMPARE(resolved.copilot_node_path, QStringLiteral("/usr/bin/node"));
    QCOMPARE(resolved.copilot_sidecar_path, QStringLiteral("/tmp/sidecar.js"));
}

void tst_completion_request_settings_t::completion_connection_settings_use_completion_overrides()
{
    settings_t settings;
    settings.provider = QStringLiteral("openai");
    settings.completion_engine = QStringLiteral("qompi");
    settings.qompi_completion_provider = QStringLiteral("local");
    settings.base_url = QStringLiteral("https://api.example.com");
    settings.api_key = QStringLiteral("global-key");
    settings.local_base_url = QStringLiteral("http://local.example");
    settings.local_endpoint_path = QStringLiteral("/v1/completions");
    settings.local_custom_headers = QStringLiteral("X-Test: one");
    settings.completion_base_url = QStringLiteral("https://completion.example.com");
    settings.completion_api_key = QStringLiteral("completion-key");
    settings.completion_local_base_url = QStringLiteral("http://completion-local.example");
    settings.completion_local_endpoint_path = QStringLiteral("/v1/complete");
    settings.completion_local_custom_headers = QStringLiteral("X-Completion: two");
    settings.completion_ollama_base_url = QStringLiteral("http://completion-ollama.example");
    settings.completion_copilot_node_path = QStringLiteral("/opt/node");
    settings.completion_copilot_sidecar_path = QStringLiteral("/opt/sidecar.js");

    const completion_connection_settings_t resolved =
        resolve_completion_connection_settings(settings);

    QCOMPARE(resolved.provider_id, QStringLiteral("local"));
    QCOMPARE(resolved.base_url, QStringLiteral("https://completion.example.com"));
    QCOMPARE(resolved.api_key, QStringLiteral("completion-key"));
    QCOMPARE(resolved.local_base_url, QStringLiteral("http://completion-local.example"));
    QCOMPARE(resolved.local_endpoint_path, QStringLiteral("/v1/complete"));
    QCOMPARE(resolved.local_custom_headers, QStringLiteral("X-Completion: two"));
    QCOMPARE(resolved.ollama_base_url, QStringLiteral("http://completion-ollama.example"));
    QCOMPARE(resolved.copilot_node_path, QStringLiteral("/opt/node"));
    QCOMPARE(resolved.copilot_sidecar_path, QStringLiteral("/opt/sidecar.js"));
}

}  // namespace qcai2

QTEST_GUILESS_MAIN(qcai2::tst_completion_request_settings_t)

#include "tst_completion_request_settings.moc"
