/*! @file
    @brief Regression tests for qompi provider/model profile resolution helpers.
*/

#include <qompi/completion_profiles.h>

#include <QtTest>

namespace qompi
{

class tst_qompi_completion_profiles_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Explicit model entries should provide defaults and capability filtering.
     */
    void resolve_completion_config_applies_model_defaults();

    /**
     * Missing model ids should fall back to the provider default model.
     */
    void resolve_completion_config_uses_provider_default_model();

    /**
     * Runtime resolution should clamp token limits and plan stop words.
     */
    void resolve_completion_config_plans_stop_words_and_clamps_limits();
};

void tst_qompi_completion_profiles_t::resolve_completion_config_applies_model_defaults()
{
    provider_profile_t provider_profile;
    provider_profile.provider_id = "openai";
    provider_profile.display_name = "OpenAI-compatible";
    provider_profile.default_model_id = "gpt-4.1-mini";

    model_profile_t model_profile;
    model_profile.model_id = "gpt-5";
    model_profile.display_name = "GPT-5";
    model_profile.default_temperature = 0.15;
    model_profile.capabilities.supports_streaming = true;
    model_profile.capabilities.supports_reasoning_effort = false;
    model_profile.capabilities.supports_thinking_level = false;
    model_profile.limits.default_max_output_tokens = 512;
    model_profile.limits.max_output_tokens_limit = 512;
    provider_profile.models.push_back(model_profile);

    completion_request_t request;
    completion_options_t options;
    options.model = "gpt-5";
    options.reasoning_effort = "high";
    options.thinking_level = "medium";

    const resolved_completion_config_t resolved_config =
        resolve_completion_config(provider_profile, request, options);

    QCOMPARE(QString::fromStdString(resolved_config.model_id), QStringLiteral("gpt-5"));
    QVERIFY(resolved_config.options.max_output_tokens.has_value());
    QVERIFY(resolved_config.options.temperature.has_value());
    QCOMPARE(resolved_config.options.max_output_tokens.value(), 512);
    QCOMPARE(resolved_config.options.temperature.value(), 0.15);
    QCOMPARE(QString::fromStdString(resolved_config.options.reasoning_effort), QString());
    QCOMPARE(QString::fromStdString(resolved_config.options.thinking_level), QString());
}

void tst_qompi_completion_profiles_t::resolve_completion_config_uses_provider_default_model()
{
    provider_profile_t provider_profile;
    provider_profile.provider_id = "copilot";
    provider_profile.display_name = "GitHub Copilot";
    provider_profile.default_model_id = "gpt-4o-mini";
    provider_profile.default_capabilities.supports_streaming = true;
    provider_profile.default_capabilities.supports_reasoning_effort = false;
    provider_profile.default_limits.default_max_output_tokens = 128;

    completion_request_t request;
    completion_options_t options;
    const resolved_completion_config_t resolved_config =
        resolve_completion_config(provider_profile, request, options);

    QCOMPARE(QString::fromStdString(resolved_config.model_id), QStringLiteral("gpt-4o-mini"));
    QCOMPARE(QString::fromStdString(resolved_config.options.model), QStringLiteral("gpt-4o-mini"));
}

void tst_qompi_completion_profiles_t::
    resolve_completion_config_plans_stop_words_and_clamps_limits()
{
    provider_profile_t provider_profile;
    provider_profile.provider_id = "openai";
    provider_profile.display_name = "OpenAI-compatible";
    provider_profile.default_model_id = "gpt-4.1-mini";
    provider_profile.default_capabilities.supports_stop_words = true;
    provider_profile.default_limits.default_max_output_tokens = 256;
    provider_profile.default_limits.max_output_tokens_limit = 128;
    provider_profile.default_limits.max_stop_words = 4;
    provider_profile.provider_stop_words = {"\n\n"};

    completion_request_t request;
    request.prefix = "call";
    request.suffix = ");\nnext_line";

    completion_options_t options;
    options.max_output_tokens = 500;
    options.stop_words = {"<END>", "<ALT>"};

    const resolved_completion_config_t resolved_config =
        resolve_completion_config(provider_profile, request, options);

    QVERIFY(resolved_config.options.max_output_tokens.has_value());
    QCOMPARE(resolved_config.options.max_output_tokens.value(), 128);
    QCOMPARE(resolved_config.stop_policy.stop_words.size(), std::size_t(4));
    QCOMPARE(QString::fromStdString(resolved_config.stop_policy.stop_words[0]),
             QStringLiteral("\n\n"));
    QCOMPARE(QString::fromStdString(resolved_config.stop_policy.stop_words[3]),
             QStringLiteral(");"));
}

}  // namespace qompi

QTEST_MAIN(qompi::tst_qompi_completion_profiles_t)

#include "tst_qompi_completion_profiles.moc"
