#include "settings.h"
#include "../util/logger.h"
#include "../util/migration.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>

/*! Implements persistent plugin settings and model catalog helpers. */

namespace qcai2
{

/** QSettings group used for all plugin settings. */
static const QLatin1String kGroup("qcai2");

namespace
{

/**
 * Trims, deduplicates, and preserves model names for editable combo boxes.
 * @param models Candidate model names.
 * @return Normalized model names with empty entries removed.
 */
QStringList normalize_model_list(const QStringList &models)
{
    QStringList normalized;
    normalized.reserve(models.size());

    for (const QString &model : models)
    {
        const QString trimmed = model.trimmed();
        if (trimmed.isEmpty() || normalized.contains(trimmed))
        {
            continue;
        }
        normalized.append(trimmed);
    }

    return normalized;
}

QJsonObject mcpServerConnectionStatesToJson(const mcp_server_connection_states_t &states)
{
    QJsonObject root;
    for (auto it = states.cbegin(); it != states.cend(); ++it)
    {
        QJsonObject stateObject;
        if (((!it.value().state.trimmed().isEmpty()) == true))
        {
            stateObject.insert(QStringLiteral("state"), it.value().state.trimmed());
        }
        if (((!it.value().message.trimmed().isEmpty()) == true))
        {
            stateObject.insert(QStringLiteral("message"), it.value().message.trimmed());
        }
        root.insert(it.key(), stateObject);
    }
    return root;
}

bool mcpServerConnectionStatesFromJson(const QJsonValue &value,
                                       mcp_server_connection_states_t *states,
                                       QString *error = nullptr)
{
    if (((states == nullptr) == true))
    {
        return false;
    }

    states->clear();
    if (((value.isUndefined() || value.isNull()) == true))
    {
        return true;
    }
    if (value.isObject() == false)
    {
        if (((error != nullptr) == true))
        {
            *error =
                QStringLiteral("Structured settings key 'mcpConnectionStates' must be an object.");
        }
        return false;
    }

    const QJsonObject connectionStatesObject = value.toObject();
    for (auto it = connectionStatesObject.begin(); ((it != connectionStatesObject.end()) == true);
         ++it)
    {
        if (((!it.value().isObject()) == true))
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral(
                             "Structured settings key 'mcpConnectionStates.%1' must be an object.")
                             .arg(it.key());
            }
            return false;
        }

        const QJsonObject stateObject = it.value().toObject();
        mcp_server_connection_state_t state;
        const QJsonValue stateValue = stateObject.value(QStringLiteral("state"));
        if (((!stateValue.isUndefined() && !stateValue.isNull() && !stateValue.isString()) ==
             true))
        {
            if (((error != nullptr) == true))
            {
                *error =
                    QStringLiteral(
                        "Structured settings key 'mcpConnectionStates.%1.state' must be a string.")
                        .arg(it.key());
            }
            return false;
        }

        const QJsonValue messageValue = stateObject.value(QStringLiteral("message"));
        if (((!messageValue.isUndefined() && !messageValue.isNull() && !messageValue.isString()) ==
             true))
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("Structured settings key 'mcpConnectionStates.%1.message' "
                                        "must be a string.")
                             .arg(it.key());
            }
            return false;
        }

        state.state = stateValue.toString().trimmed();
        state.message = messageValue.toString().trimmed();
        states->insert(it.key(), state);
    }

    return true;
}

QJsonObject readStructuredSettingsFile(const QString &path, bool *exists = nullptr,
                                       QString *error = nullptr)
{
    QFile file(path);
    if (file.exists() == false)
    {
        if (((exists != nullptr) == true))
        {
            *exists = false;
        }
        return {};
    }

    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (((exists != nullptr) == true))
        {
            *exists = true;
        }
        if (((error != nullptr) == true))
        {
            *error =
                QStringLiteral("Failed to open %1 for reading: %2").arg(path, file.errorString());
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (((parseError.error != QJsonParseError::NoError || !document.isObject()) == true))
    {
        if (((exists != nullptr) == true))
        {
            *exists = true;
        }
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to parse structured settings JSON %1: %2")
                         .arg(path, parseError.error == QJsonParseError::NoError
                                        ? QStringLiteral("root must be an object")
                                        : parseError.errorString());
        }
        return {};
    }

    if (((exists != nullptr) == true))
    {
        *exists = true;
    }
    return document.object();
}

bool writeStructuredSettingsFile(const QString &path, const QJsonObject &root,
                                 QString *error = nullptr)
{
    const QFileInfo fileInfo(path);
    if (((!QDir().mkpath(fileInfo.absolutePath())) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to create directory for %1").arg(path);
        }
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) == false)
    {
        if (((error != nullptr) == true))
        {
            *error =
                QStringLiteral("Failed to open %1 for writing: %2").arg(path, file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Failed to commit %1").arg(path);
        }
        return false;
    }

    return true;
}

}  // namespace

void settings_t::load()
{
    QSettings s;
    s.beginGroup(kGroup);
    QString migrationError;
    const bool migrationOk = Migration::migrate_global_settings(s, &migrationError);
    if ((((!migrationOk || !migrationError.isEmpty()) && !migrationError.isEmpty()) == true))
    {
        QCAI_WARN("settings_t", migrationError);
    }

    provider = s.value("provider", provider).toString();
    base_url = s.value("baseUrl", base_url).toString();
    // Note: apiKey is loaded but never logged
    api_key = s.value("apiKey", api_key).toString();
    model_name = s.value("modelName", model_name).toString();
    thinking_level = s.value("thinkingLevel", thinking_level).toString();
    reasoning_effort = s.value("reasoningEffort", thinking_level).toString();
    temperature = s.value("temperature", temperature).toDouble();
    max_tokens = s.value("maxTokens", max_tokens).toInt();

    local_base_url = s.value("localBaseUrl", local_base_url).toString();
    local_endpoint_path = s.value("localEndpointPath", local_endpoint_path).toString();
    local_simple_mode = s.value("localSimpleMode", local_simple_mode).toBool();
    local_custom_headers = s.value("localCustomHeaders", local_custom_headers).toString();

    ollama_base_url = s.value("ollamaBaseUrl", ollama_base_url).toString();
    ollama_model = s.value("ollamaModel", ollama_model).toString();

    copilot_token = s.value("copilotToken", copilot_token).toString();
    copilot_model = s.value("copilotModel", copilot_model).toString();
    copilot_node_path = s.value("copilotNodePath", copilot_node_path).toString();
    copilot_sidecar_path = s.value("copilotSidecarPath", copilot_sidecar_path).toString();
    copilot_completion_timeout_sec =
        s.value("copilotCompletionTimeoutSec", copilot_completion_timeout_sec).toInt();

    max_iterations = s.value("maxIterations", max_iterations).toInt();
    max_tool_calls = s.value("maxToolCalls", max_tool_calls).toInt();
    max_diff_lines = s.value("maxDiffLines", max_diff_lines).toInt();
    max_changed_files = s.value("maxChangedFiles", max_changed_files).toInt();

    dry_run_default = s.value("dryRunDefault", dry_run_default).toBool();
    ai_completion_enabled = s.value("aiCompletionEnabled", ai_completion_enabled).toBool();
    debug_logging = s.value("debugLogging", debug_logging).toBool();
    detailed_request_logging =
        s.value("detailedRequestLogging", detailed_request_logging).toBool();
    agent_debug = s.value("agentDebug", agent_debug).toBool();
    completion_min_chars = s.value("completionMinChars", completion_min_chars).toInt();
    completion_delay_ms = s.value("completionDelayMs", completion_delay_ms).toInt();
    completion_model = s.value("completionModel", completion_model).toString();
    completion_thinking_level =
        s.value("completionThinkingLevel", completion_thinking_level).toString();
    completion_reasoning_effort =
        s.value("completionReasoningEffort", completion_reasoning_effort).toString();

    s.endGroup();

    mcp_servers.clear();
    bool structuredSettingsExist = false;
    QString structuredError;
    const QJsonObject structuredRoot =
        readStructuredSettingsFile(Migration::global_structured_settings_file_path(),
                                   &structuredSettingsExist, &structuredError);
    if (structuredError.isEmpty() == false)
    {
        QCAI_WARN("settings_t", structuredError);
    }
    else if (structuredSettingsExist == true)
    {
        const QJsonValue mcpServersValue = structuredRoot.value(QStringLiteral("mcpServers"));
        if (((mcpServersValue.isUndefined() || mcpServersValue.isNull()) == true))
        {
            mcp_servers.clear();
        }
        else if (mcpServersValue.isObject() == false)
        {
            QCAI_WARN("settings_t",
                      QStringLiteral("Structured settings key 'mcpServers' must be an object."));
        }
        else
        {
            QString mcpError;
            if (((!qtmcp::server_definitions_from_json(mcpServersValue.toObject(), &mcp_servers,
                                                       &mcpError)) == true))
            {
                QCAI_WARN("settings_t", mcpError);
            }
        }
    }
    QCAI_INFO("settings_t",
              QStringLiteral("Loaded: provider=%1 model=%2 debug=%3")
                  .arg(provider, model_name,
                       debug_logging ? QStringLiteral("on") : QStringLiteral("off")));
}

void settings_t::save() const
{
    QSettings s;
    s.beginGroup(kGroup);

    s.setValue("provider", provider);
    s.setValue("baseUrl", base_url);
    s.setValue("apiKey", api_key);
    s.setValue("modelName", model_name);
    s.setValue("reasoningEffort", reasoning_effort);
    s.setValue("thinkingLevel", thinking_level);
    s.setValue("temperature", temperature);
    s.setValue("maxTokens", max_tokens);

    s.setValue("localBaseUrl", local_base_url);
    s.setValue("localEndpointPath", local_endpoint_path);
    s.setValue("localSimpleMode", local_simple_mode);
    s.setValue("localCustomHeaders", local_custom_headers);

    s.setValue("ollamaBaseUrl", ollama_base_url);
    s.setValue("ollamaModel", ollama_model);

    s.setValue("copilotToken", copilot_token);
    s.setValue("copilotModel", copilot_model);
    s.setValue("copilotNodePath", copilot_node_path);
    s.setValue("copilotSidecarPath", copilot_sidecar_path);
    s.setValue("copilotCompletionTimeoutSec", copilot_completion_timeout_sec);

    s.setValue("maxIterations", max_iterations);
    s.setValue("maxToolCalls", max_tool_calls);
    s.setValue("maxDiffLines", max_diff_lines);
    s.setValue("maxChangedFiles", max_changed_files);

    s.setValue("dryRunDefault", dry_run_default);
    s.setValue("aiCompletionEnabled", ai_completion_enabled);
    s.setValue("debugLogging", debug_logging);
    s.setValue("detailedRequestLogging", detailed_request_logging);
    s.setValue("agentDebug", agent_debug);
    s.setValue("completionMinChars", completion_min_chars);
    s.setValue("completionDelayMs", completion_delay_ms);
    s.setValue("completionModel", completion_model);
    s.setValue("completionThinkingLevel", completion_thinking_level);
    s.setValue("completionReasoningEffort", completion_reasoning_effort);
    Migration::stamp_global_settings(s);

    s.endGroup();

    const QString structuredSettingsPath = Migration::global_structured_settings_file_path();
    bool structuredSettingsExist = false;
    QString structuredError;
    QJsonObject structuredRoot = readStructuredSettingsFile(
        structuredSettingsPath, &structuredSettingsExist, &structuredError);
    if (structuredError.isEmpty() == false)
    {
        QCAI_WARN("settings_t", structuredError);
        structuredRoot = QJsonObject{};
    }

    if (mcp_servers.isEmpty() == true)
    {
        structuredRoot.remove(QStringLiteral("mcpServers"));
    }
    else
    {
        structuredRoot.insert(QStringLiteral("mcpServers"),
                              qtmcp::server_definitions_to_json(mcp_servers));
    }

    if (structuredRoot.isEmpty() == true)
    {
        QFile::remove(structuredSettingsPath);
    }
    else
    {
        structuredError.clear();
        if (((writeStructuredSettingsFile(structuredSettingsPath, structuredRoot,
                                          &structuredError)) == false))
        {
            QCAI_WARN("settings_t", structuredError);
        }
    }

    QCAI_DEBUG("settings_t", QStringLiteral("settings_t saved"));
}

settings_t &settings()
{
    static settings_t s;
    return s;
}

mcp_server_connection_states_t load_mcp_server_connection_states(QString *error)
{
    mcp_server_connection_states_t states;
    bool structuredSettingsExist = false;
    QString structuredError;
    const QJsonObject structuredRoot =
        readStructuredSettingsFile(Migration::global_structured_settings_file_path(),
                                   &structuredSettingsExist, &structuredError);
    if (structuredError.isEmpty() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = structuredError;
        }
        return states;
    }

    if (structuredSettingsExist == false)
    {
        return states;
    }

    QString parseError;
    if (!mcpServerConnectionStatesFromJson(
            structuredRoot.value(QStringLiteral("mcpConnectionStates")), &states, &parseError))
    {
        if (((error != nullptr) == true))
        {
            *error = parseError;
        }
    }

    return states;
}

bool save_mcp_server_connection_states(const mcp_server_connection_states_t &states,
                                       QString *error)
{
    const QString structuredSettingsPath = Migration::global_structured_settings_file_path();
    bool structuredSettingsExist = false;
    QString structuredError;
    QJsonObject structuredRoot = readStructuredSettingsFile(
        structuredSettingsPath, &structuredSettingsExist, &structuredError);
    if (structuredError.isEmpty() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = structuredError;
        }
        return false;
    }

    if (states.isEmpty())
    {
        structuredRoot.remove(QStringLiteral("mcpConnectionStates"));
    }
    else
    {
        structuredRoot.insert(QStringLiteral("mcpConnectionStates"),
                              mcpServerConnectionStatesToJson(states));
    }

    if (structuredRoot.isEmpty() == true)
    {
        return !QFile::exists(structuredSettingsPath) || QFile::remove(structuredSettingsPath);
    }

    return writeStructuredSettingsFile(structuredSettingsPath, structuredRoot, error);
}

model_catalog_t::model_catalog_t(QObject *parent)
    : QObject(parent), available_copilot_models(default_copilot_models())
{
}

QStringList model_catalog_t::copilot_models() const
{
    return this->available_copilot_models;
}

void model_catalog_t::set_copilot_models(const QStringList &models)
{
    const QStringList normalized = normalize_model_list(models);
    if (((normalized.isEmpty() || normalized == this->available_copilot_models) == true))
    {
        return;
    }

    this->available_copilot_models = normalized;
    emit this->copilot_models_changed(this->available_copilot_models);
}

QStringList model_catalog_t::default_copilot_models()
{
    return {
        QStringLiteral("gpt-5.4"),           QStringLiteral("gpt-5.4"),
        QStringLiteral("gpt-5.3-codex"),     QStringLiteral("gpt-5.2-codex"),
        QStringLiteral("gpt-5.2"),           QStringLiteral("gpt-5.1-codex-max"),
        QStringLiteral("gpt-5.1-codex"),     QStringLiteral("gpt-5.1-codex-mini"),
        QStringLiteral("gpt-5.1"),           QStringLiteral("gpt-5-mini"),
        QStringLiteral("gpt-4.1"),           QStringLiteral("gpt-4o"),
        QStringLiteral("o4-mini"),           QStringLiteral("o3"),
        QStringLiteral("claude-opus-4.6"),   QStringLiteral("claude-opus-4.6-fast"),
        QStringLiteral("claude-opus-4.5"),   QStringLiteral("claude-sonnet-4.6"),
        QStringLiteral("claude-sonnet-4.5"), QStringLiteral("claude-sonnet-4"),
        QStringLiteral("claude-haiku-4.5"),  QStringLiteral("gemini-3.1-pro"),
        QStringLiteral("gemini-3-pro"),      QStringLiteral("gemini-3-flash"),
        QStringLiteral("gemini-2.5-pro"),    QStringLiteral("grok-code-fast-1"),
    };
}

model_catalog_t &model_catalog()
{
    static model_catalog_t catalog;
    return catalog;
}

}  // namespace qcai2
