/*! @file
    @brief Implements the qcai2 callback bridge for qompi completion execution.
*/

#include "qompi_provider_backend_adapter.h"

#include "../models/agent_messages.h"
#include "../progress/agent_progress.h"

#include "qompi/callback_completion_backend.h"
#include "qompi/completion_operation.h"
#include "qompi/completion_policy.h"

#include <QList>

#include <memory>

namespace qcai2
{

namespace
{

QString from_std_string(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string to_std_string(const QString &value)
{
    return value.toUtf8().toStdString();
}

void emit_qompi_debug_event(const std::shared_ptr<qompi::completion_sink_t> &sink,
                            const QString &stage, const QString &message)
{
    if (sink == nullptr)
    {
        return;
    }
    sink->on_debug_event(to_std_string(stage), to_std_string(message));
}

QList<chat_message_t> build_messages(const qompi::completion_request_t &request)
{
    QList<chat_message_t> messages;
    messages.reserve(static_cast<qsizetype>(request.system_context_blocks.size()) + 1);
    for (const std::string &block : request.system_context_blocks)
    {
        messages.append({QStringLiteral("system"), from_std_string(block)});
    }
    messages.append({QStringLiteral("user"), from_std_string(request.user_prompt)});
    return messages;
}

qompi::provider_profile_t build_openai_like_profile(const QString &provider_id,
                                                    const QString &display_name,
                                                    const QString &default_model)
{
    qompi::provider_profile_t profile;
    profile.provider_id = to_std_string(provider_id);
    profile.display_name = to_std_string(display_name);
    profile.default_model_id = to_std_string(default_model);
    profile.default_temperature = 0.0;
    profile.default_capabilities.supports_streaming = true;
    profile.default_capabilities.supports_cancellation = false;
    profile.default_capabilities.supports_reasoning_effort = true;
    profile.default_capabilities.supports_thinking_level = false;
    profile.default_capabilities.supports_fill_in_the_middle = true;
    profile.default_capabilities.supports_temperature = true;
    profile.default_capabilities.supports_seed = false;
    profile.default_capabilities.supports_stop_words = true;
    profile.default_limits.default_max_output_tokens = 128;
    profile.default_limits.max_output_tokens_limit = 128;
    profile.default_limits.max_stop_words = 4;

    qompi::model_profile_t model_profile;
    model_profile.model_id = profile.default_model_id;
    model_profile.display_name = profile.default_model_id;
    model_profile.default_temperature = 0.0;
    model_profile.capabilities = profile.default_capabilities;
    model_profile.limits = profile.default_limits;
    profile.models.push_back(std::move(model_profile));
    return profile;
}

qompi::provider_profile_t build_copilot_profile(const QString &default_model)
{
    qompi::provider_profile_t profile;
    profile.provider_id = "copilot";
    profile.display_name = "GitHub Copilot";
    profile.default_model_id = to_std_string(default_model);
    profile.default_temperature = 0.0;
    profile.default_capabilities.supports_streaming = true;
    profile.default_capabilities.supports_cancellation = false;
    profile.default_capabilities.supports_reasoning_effort = false;
    profile.default_capabilities.supports_thinking_level = false;
    profile.default_capabilities.supports_fill_in_the_middle = true;
    profile.default_capabilities.supports_temperature = false;
    profile.default_capabilities.supports_seed = false;
    profile.default_capabilities.supports_stop_words = false;
    profile.default_limits.default_max_output_tokens = 128;
    profile.default_limits.max_output_tokens_limit = 128;
    profile.default_limits.max_stop_words = 0;

    qompi::model_profile_t model_profile;
    model_profile.model_id = profile.default_model_id;
    model_profile.display_name = profile.default_model_id;
    model_profile.default_temperature = 0.0;
    model_profile.capabilities = profile.default_capabilities;
    model_profile.limits = profile.default_limits;
    profile.models.push_back(std::move(model_profile));
    return profile;
}

}  // namespace

std::shared_ptr<qompi::completion_backend_t>
create_qompi_provider_backend(iai_provider_t *provider)
{
    return qompi::create_callback_completion_backend(
        [provider](const qompi::completion_request_t &request,
                   const qompi::completion_options_t &options,
                   std::shared_ptr<qompi::completion_sink_t> sink) {
            if (provider == nullptr)
            {
                emit_qompi_debug_event(sink, QStringLiteral("provider.unavailable"),
                                       QStringLiteral("qompi backend received a null provider."));
                qompi::completion_error_t error;
                error.code = -1;
                error.message = "Completion provider is unavailable.";
                error.is_retryable = false;
                error.is_cancellation = false;
                sink->on_failed(error);
                return qompi::create_callback_completion_operation(request.request_id);
            }

            const QList<chat_message_t> messages = build_messages(request);
            const qompi::completion_stop_policy_t stop_policy =
                qompi::build_completion_stop_policy(request, options);
            auto streaming_state = std::make_shared<qompi::completion_stream_state_t>();
            emit_qompi_debug_event(
                sink, QStringLiteral("provider.dispatch_started"),
                QStringLiteral("provider=%1 model=%2 messages=%3 max_output_tokens=%4 "
                               "reasoning=%5 stop_words=%6 streaming_transport=on")
                    .arg(provider->id(), from_std_string(options.model))
                    .arg(messages.size())
                    .arg(options.max_output_tokens.has_value() == true
                             ? QString::number(options.max_output_tokens.value())
                             : QStringLiteral("-"))
                    .arg(options.reasoning_effort.empty() == false
                             ? from_std_string(options.reasoning_effort)
                             : QStringLiteral("-"))
                    .arg(QString::number(static_cast<qulonglong>(stop_policy.stop_words.size()))));
            sink->on_started(request.request_id);
            provider->complete(
                messages, from_std_string(options.model), options.temperature.value_or(0.0),
                static_cast<int>(options.max_output_tokens.value_or(128)),
                from_std_string(options.reasoning_effort),
                [sink, request, stop_policy, streaming_state](
                    const QString &response, const QString &error, const provider_usage_t &) {
                    if (error.isEmpty() == false)
                    {
                        emit_qompi_debug_event(sink, QStringLiteral("provider.failed"),
                                               QStringLiteral("error=%1").arg(error));
                        qompi::completion_error_t qompi_error;
                        qompi_error.code = -1;
                        qompi_error.message = to_std_string(error);
                        qompi_error.is_retryable = false;
                        qompi_error.is_cancellation = false;
                        sink->on_failed(qompi_error);
                        return;
                    }

                    const std::string raw_response = response.isEmpty() == false
                                                         ? to_std_string(response)
                                                         : streaming_state->accumulated_response;
                    bool matched_stop_word = false;
                    const std::string normalized_completion = qompi::normalize_completion_response(
                        request.prefix, request.suffix, raw_response);
                    const std::string final_completion = qompi::apply_completion_stop_policy(
                        normalized_completion, stop_policy, &matched_stop_word);
                    emit_qompi_debug_event(
                        sink, QStringLiteral("provider.callback_received"),
                        QStringLiteral("callback_chars=%1 raw_response_chars=%2 "
                                       "normalized_chars=%3 final_chars=%4 "
                                       "used_stream_fallback=%5 matched_stop_word=%6")
                            .arg(response.size())
                            .arg(QString::number(static_cast<qulonglong>(raw_response.size())))
                            .arg(QString::number(
                                static_cast<qulonglong>(normalized_completion.size())))
                            .arg(QString::number(static_cast<qulonglong>(final_completion.size())))
                            .arg(response.isEmpty() == true && raw_response.empty() == false
                                     ? QStringLiteral("true")
                                     : QStringLiteral("false"))
                            .arg(matched_stop_word == true ? QStringLiteral("true")
                                                           : QStringLiteral("false")));
                    qompi::completion_result_t result;
                    result.request_id = request.request_id;
                    result.text = final_completion;
                    if (matched_stop_word == true)
                    {
                        result.finish_reason = "stop_word";
                    }
                    sink->on_completed(result);
                },
                [sink](const QString &delta) {
                    if (delta.isEmpty() == true)
                    {
                        return;
                    }
                    emit_qompi_debug_event(sink, QStringLiteral("provider.stream_callback_delta"),
                                           QStringLiteral("delta_chars=%1").arg(delta.size()));
                },
                [sink, request, stop_policy, streaming_state](const provider_raw_event_t &event) {
                    emit_qompi_debug_event(
                        sink, QStringLiteral("provider.raw_event"),
                        QStringLiteral("kind=%1 raw_type=%2 tool=%3 message_chars=%4")
                            .arg(provider_raw_event_kind_name(event.kind), event.raw_type,
                                 event.tool_name.isEmpty() == false ? event.tool_name
                                                                    : QStringLiteral("-"))
                            .arg(event.message.size()));
                    if (event.kind != provider_raw_event_kind_t::MESSAGE_DELTA ||
                        event.message.isEmpty() == true)
                    {
                        return;
                    }

                    const qompi::completion_stream_update_t update =
                        qompi::aggregate_completion_stream(
                            *streaming_state, request, to_std_string(event.message), stop_policy);
                    if (update.has_chunk == false)
                    {
                        return;
                    }
                    emit_qompi_debug_event(
                        sink, QStringLiteral("provider.chunk_ready"),
                        QStringLiteral("stable_chars=%1 delta_chars=%2 normalized_chars=%3 "
                                       "matched_stop_word=%4")
                            .arg(QString::number(
                                static_cast<qulonglong>(update.chunk.stable_text.size())))
                            .arg(QString::number(
                                static_cast<qulonglong>(update.chunk.text_delta.size())))
                            .arg(QString::number(
                                static_cast<qulonglong>(update.normalized_completion.size())))
                            .arg(update.matched_stop_word == true ? QStringLiteral("true")
                                                                  : QStringLiteral("false")));
                    sink->on_chunk(update.chunk);
                });

            return qompi::create_callback_completion_operation(request.request_id);
        });
}

qompi::provider_profile_t create_qompi_provider_profile(iai_provider_t *provider,
                                                        const QString &default_model)
{
    if (provider == nullptr)
    {
        qompi::provider_profile_t profile;
        profile.default_model_id = to_std_string(default_model);
        return profile;
    }

    const QString provider_id = provider->id();
    if (provider_id == QStringLiteral("copilot"))
    {
        return build_copilot_profile(default_model);
    }
    if (provider_id == QStringLiteral("openai"))
    {
        return build_openai_like_profile(provider_id, provider->display_name(), default_model);
    }
    if (provider_id == QStringLiteral("anthropic"))
    {
        qompi::provider_profile_t profile =
            build_openai_like_profile(provider_id, provider->display_name(), default_model);
        profile.default_capabilities.supports_thinking_level = true;
        profile.models.front().limits.max_output_tokens_limit = 128;
        profile.models.front().capabilities = profile.default_capabilities;
        return profile;
    }
    if (provider_id == QStringLiteral("local") || provider_id == QStringLiteral("ollama"))
    {
        qompi::provider_profile_t profile =
            build_openai_like_profile(provider_id, provider->display_name(), default_model);
        profile.default_capabilities.supports_reasoning_effort = false;
        profile.default_capabilities.supports_stop_words = false;
        profile.models.front().capabilities = profile.default_capabilities;
        return profile;
    }

    return build_openai_like_profile(provider_id, provider->display_name(), default_model);
}

}  // namespace qcai2
