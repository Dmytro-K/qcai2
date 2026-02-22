#include "Settings.h"
#include "../util/Logger.h"

#include <QSettings>

namespace Qcai2 {

static const QLatin1String kGroup("Qcai2");

void Settings::load()
{
    QSettings s;
    s.beginGroup(kGroup);

    provider        = s.value("provider",        provider).toString();
    baseUrl         = s.value("baseUrl",         baseUrl).toString();
    // Note: apiKey is loaded but never logged
    apiKey          = s.value("apiKey",           apiKey).toString();
    modelName       = s.value("modelName",       modelName).toString();
    thinkingLevel   = s.value("thinkingLevel",   thinkingLevel).toString();
    temperature     = s.value("temperature",     temperature).toDouble();
    maxTokens       = s.value("maxTokens",       maxTokens).toInt();

    localBaseUrl        = s.value("localBaseUrl",        localBaseUrl).toString();
    localEndpointPath   = s.value("localEndpointPath",   localEndpointPath).toString();
    localSimpleMode     = s.value("localSimpleMode",     localSimpleMode).toBool();
    localCustomHeaders  = s.value("localCustomHeaders",  localCustomHeaders).toString();

    ollamaBaseUrl   = s.value("ollamaBaseUrl",   ollamaBaseUrl).toString();
    ollamaModel     = s.value("ollamaModel",     ollamaModel).toString();

    copilotToken       = s.value("copilotToken",       copilotToken).toString();
    copilotModel       = s.value("copilotModel",       copilotModel).toString();
    copilotNodePath    = s.value("copilotNodePath",    copilotNodePath).toString();
    copilotSidecarPath = s.value("copilotSidecarPath", copilotSidecarPath).toString();

    maxIterations   = s.value("maxIterations",   maxIterations).toInt();
    maxToolCalls    = s.value("maxToolCalls",     maxToolCalls).toInt();
    maxDiffLines    = s.value("maxDiffLines",     maxDiffLines).toInt();
    maxChangedFiles = s.value("maxChangedFiles",  maxChangedFiles).toInt();

    dryRunDefault   = s.value("dryRunDefault",   dryRunDefault).toBool();
    aiCompletionEnabled = s.value("aiCompletionEnabled", aiCompletionEnabled).toBool();
    debugLogging    = s.value("debugLogging",   debugLogging).toBool();
    agentDebug      = s.value("agentDebug",     agentDebug).toBool();
    completionMinChars = s.value("completionMinChars", completionMinChars).toInt();
    completionDelayMs  = s.value("completionDelayMs",  completionDelayMs).toInt();
    completionModel    = s.value("completionModel",    completionModel).toString();

    s.endGroup();
    QCAI_INFO("Settings", QStringLiteral("Loaded: provider=%1 model=%2 debug=%3")
        .arg(provider, modelName, debugLogging ? QStringLiteral("on") : QStringLiteral("off")));
}

void Settings::save() const
{
    QSettings s;
    s.beginGroup(kGroup);

    s.setValue("provider",          provider);
    s.setValue("baseUrl",           baseUrl);
    s.setValue("apiKey",            apiKey);
    s.setValue("modelName",         modelName);
    s.setValue("thinkingLevel",     thinkingLevel);
    s.setValue("temperature",       temperature);
    s.setValue("maxTokens",         maxTokens);

    s.setValue("localBaseUrl",       localBaseUrl);
    s.setValue("localEndpointPath",  localEndpointPath);
    s.setValue("localSimpleMode",    localSimpleMode);
    s.setValue("localCustomHeaders", localCustomHeaders);

    s.setValue("ollamaBaseUrl",     ollamaBaseUrl);
    s.setValue("ollamaModel",       ollamaModel);

    s.setValue("copilotToken",         copilotToken);
    s.setValue("copilotModel",         copilotModel);
    s.setValue("copilotNodePath",      copilotNodePath);
    s.setValue("copilotSidecarPath",   copilotSidecarPath);

    s.setValue("maxIterations",     maxIterations);
    s.setValue("maxToolCalls",      maxToolCalls);
    s.setValue("maxDiffLines",      maxDiffLines);
    s.setValue("maxChangedFiles",   maxChangedFiles);

    s.setValue("dryRunDefault",     dryRunDefault);
    s.setValue("aiCompletionEnabled", aiCompletionEnabled);
    s.setValue("debugLogging",     debugLogging);
    s.setValue("agentDebug",       agentDebug);
    s.setValue("completionMinChars", completionMinChars);
    s.setValue("completionDelayMs",  completionDelayMs);
    s.setValue("completionModel",    completionModel);

    s.endGroup();
    QCAI_DEBUG("Settings", QStringLiteral("Settings saved"));
}

Settings &settings()
{
    static Settings s;
    return s;
}

} // namespace Qcai2
