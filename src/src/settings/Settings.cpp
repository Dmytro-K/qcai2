#include "Settings.h"
#include "../util/Logger.h"
#include "../util/Migration.h"

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
QStringList normalizeModelList(const QStringList &models)
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

QJsonObject mcpServerConnectionStatesToJson(const McpServerConnectionStates &states)
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

bool mcpServerConnectionStatesFromJson(const QJsonValue &value, McpServerConnectionStates *states,
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
        McpServerConnectionState state;
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

void Settings::load()
{
    QSettings s;
    s.beginGroup(kGroup);
    QString migrationError;
    const bool migrationOk = Migration::migrateGlobalSettings(s, &migrationError);
    if ((((!migrationOk || !migrationError.isEmpty()) && !migrationError.isEmpty()) == true))
    {
        QCAI_WARN("Settings", migrationError);
    }

    provider = s.value("provider", provider).toString();
    baseUrl = s.value("baseUrl", baseUrl).toString();
    // Note: apiKey is loaded but never logged
    apiKey = s.value("apiKey", apiKey).toString();
    modelName = s.value("modelName", modelName).toString();
    thinkingLevel = s.value("thinkingLevel", thinkingLevel).toString();
    reasoningEffort = s.value("reasoningEffort", thinkingLevel).toString();
    temperature = s.value("temperature", temperature).toDouble();
    maxTokens = s.value("maxTokens", maxTokens).toInt();

    localBaseUrl = s.value("localBaseUrl", localBaseUrl).toString();
    localEndpointPath = s.value("localEndpointPath", localEndpointPath).toString();
    localSimpleMode = s.value("localSimpleMode", localSimpleMode).toBool();
    localCustomHeaders = s.value("localCustomHeaders", localCustomHeaders).toString();

    ollamaBaseUrl = s.value("ollamaBaseUrl", ollamaBaseUrl).toString();
    ollamaModel = s.value("ollamaModel", ollamaModel).toString();

    copilotToken = s.value("copilotToken", copilotToken).toString();
    copilotModel = s.value("copilotModel", copilotModel).toString();
    copilotNodePath = s.value("copilotNodePath", copilotNodePath).toString();
    copilotSidecarPath = s.value("copilotSidecarPath", copilotSidecarPath).toString();

    maxIterations = s.value("maxIterations", maxIterations).toInt();
    maxToolCalls = s.value("maxToolCalls", maxToolCalls).toInt();
    maxDiffLines = s.value("maxDiffLines", maxDiffLines).toInt();
    maxChangedFiles = s.value("maxChangedFiles", maxChangedFiles).toInt();

    dryRunDefault = s.value("dryRunDefault", dryRunDefault).toBool();
    aiCompletionEnabled = s.value("aiCompletionEnabled", aiCompletionEnabled).toBool();
    debugLogging = s.value("debugLogging", debugLogging).toBool();
    agentDebug = s.value("agentDebug", agentDebug).toBool();
    completionMinChars = s.value("completionMinChars", completionMinChars).toInt();
    completionDelayMs = s.value("completionDelayMs", completionDelayMs).toInt();
    completionModel = s.value("completionModel", completionModel).toString();
    completionThinkingLevel =
        s.value("completionThinkingLevel", completionThinkingLevel).toString();
    completionReasoningEffort =
        s.value("completionReasoningEffort", completionReasoningEffort).toString();

    s.endGroup();

    mcpServers.clear();
    bool structuredSettingsExist = false;
    QString structuredError;
    const QJsonObject structuredRoot = readStructuredSettingsFile(
        Migration::globalStructuredSettingsFilePath(), &structuredSettingsExist, &structuredError);
    if (structuredError.isEmpty() == false)
    {
        QCAI_WARN("Settings", structuredError);
    }
    else if (structuredSettingsExist == true)
    {
        const QJsonValue mcpServersValue = structuredRoot.value(QStringLiteral("mcpServers"));
        if (((mcpServersValue.isUndefined() || mcpServersValue.isNull()) == true))
        {
            mcpServers.clear();
        }
        else if (mcpServersValue.isObject() == false)
        {
            QCAI_WARN("Settings",
                      QStringLiteral("Structured settings key 'mcpServers' must be an object."));
        }
        else
        {
            QString mcpError;
            if (((!qtmcp::serverDefinitionsFromJson(mcpServersValue.toObject(), &mcpServers,
                                                    &mcpError)) == true))
            {
                QCAI_WARN("Settings", mcpError);
            }
        }
    }
    QCAI_INFO("Settings", QStringLiteral("Loaded: provider=%1 model=%2 debug=%3")
                              .arg(provider, modelName,
                                   debugLogging ? QStringLiteral("on") : QStringLiteral("off")));
}

void Settings::save() const
{
    QSettings s;
    s.beginGroup(kGroup);

    s.setValue("provider", provider);
    s.setValue("baseUrl", baseUrl);
    s.setValue("apiKey", apiKey);
    s.setValue("modelName", modelName);
    s.setValue("reasoningEffort", reasoningEffort);
    s.setValue("thinkingLevel", thinkingLevel);
    s.setValue("temperature", temperature);
    s.setValue("maxTokens", maxTokens);

    s.setValue("localBaseUrl", localBaseUrl);
    s.setValue("localEndpointPath", localEndpointPath);
    s.setValue("localSimpleMode", localSimpleMode);
    s.setValue("localCustomHeaders", localCustomHeaders);

    s.setValue("ollamaBaseUrl", ollamaBaseUrl);
    s.setValue("ollamaModel", ollamaModel);

    s.setValue("copilotToken", copilotToken);
    s.setValue("copilotModel", copilotModel);
    s.setValue("copilotNodePath", copilotNodePath);
    s.setValue("copilotSidecarPath", copilotSidecarPath);

    s.setValue("maxIterations", maxIterations);
    s.setValue("maxToolCalls", maxToolCalls);
    s.setValue("maxDiffLines", maxDiffLines);
    s.setValue("maxChangedFiles", maxChangedFiles);

    s.setValue("dryRunDefault", dryRunDefault);
    s.setValue("aiCompletionEnabled", aiCompletionEnabled);
    s.setValue("debugLogging", debugLogging);
    s.setValue("agentDebug", agentDebug);
    s.setValue("completionMinChars", completionMinChars);
    s.setValue("completionDelayMs", completionDelayMs);
    s.setValue("completionModel", completionModel);
    s.setValue("completionThinkingLevel", completionThinkingLevel);
    s.setValue("completionReasoningEffort", completionReasoningEffort);
    Migration::stampGlobalSettings(s);

    s.endGroup();

    const QString structuredSettingsPath = Migration::globalStructuredSettingsFilePath();
    bool structuredSettingsExist = false;
    QString structuredError;
    QJsonObject structuredRoot = readStructuredSettingsFile(
        structuredSettingsPath, &structuredSettingsExist, &structuredError);
    if (structuredError.isEmpty() == false)
    {
        QCAI_WARN("Settings", structuredError);
        structuredRoot = QJsonObject{};
    }

    if (mcpServers.isEmpty() == true)
    {
        structuredRoot.remove(QStringLiteral("mcpServers"));
    }
    else
    {
        structuredRoot.insert(QStringLiteral("mcpServers"),
                              qtmcp::serverDefinitionsToJson(mcpServers));
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
            QCAI_WARN("Settings", structuredError);
        }
    }

    QCAI_DEBUG("Settings", QStringLiteral("Settings saved"));
}

Settings &settings()
{
    static Settings s;
    return s;
}

McpServerConnectionStates loadMcpServerConnectionStates(QString *error)
{
    McpServerConnectionStates states;
    bool structuredSettingsExist = false;
    QString structuredError;
    const QJsonObject structuredRoot = readStructuredSettingsFile(
        Migration::globalStructuredSettingsFilePath(), &structuredSettingsExist, &structuredError);
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

bool saveMcpServerConnectionStates(const McpServerConnectionStates &states, QString *error)
{
    const QString structuredSettingsPath = Migration::globalStructuredSettingsFilePath();
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

ModelCatalog::ModelCatalog(QObject *parent)
    : QObject(parent), m_copilotModels(defaultCopilotModels())
{
}

QStringList ModelCatalog::copilotModels() const
{
    return m_copilotModels;
}

void ModelCatalog::setCopilotModels(const QStringList &models)
{
    const QStringList normalized = normalizeModelList(models);
    if (((normalized.isEmpty() || normalized == m_copilotModels) == true))
    {
        return;
    }

    m_copilotModels = normalized;
    emit copilotModelsChanged(m_copilotModels);
}

QStringList ModelCatalog::defaultCopilotModels()
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

ModelCatalog &modelCatalog()
{
    static ModelCatalog catalog;
    return catalog;
}

}  // namespace qcai2
