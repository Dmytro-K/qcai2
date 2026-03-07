#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace qcai2
{

// Persistent settings for the AI Agent plugin.
// Stored via QSettings (Qt Creator settings).
struct Settings
{
    // Provider settings
    QString provider = QStringLiteral("openai");
    QString baseUrl = QStringLiteral("https://api.openai.com");
    QString apiKey;
    QString modelName = QStringLiteral("gpt-5.2");
    QString thinkingLevel = QStringLiteral("medium");  // off|low|medium|high
    double temperature = 0.2;
    int maxTokens = 4096;

    // Local provider
    QString localBaseUrl = QStringLiteral("http://localhost:8080");
    QString localEndpointPath = QStringLiteral("/v1/chat/completions");
    bool localSimpleMode = false;
    QString localCustomHeaders;

    // Ollama
    QString ollamaBaseUrl = QStringLiteral("http://localhost:11434");
    QString ollamaModel = QStringLiteral("llama4");

    // GitHub Copilot (via Node.js sidecar + @github/copilot-sdk)
    QString copilotToken;
    QString copilotModel = QStringLiteral("gpt-4o");
    QString copilotNodePath;     // path to node binary (empty = "node" from PATH)
    QString copilotSidecarPath;  // path to copilot-sidecar.js (empty = auto-detect)

    // Safety limits
    int maxIterations = 8;
    int maxToolCalls = 25;
    int maxDiffLines = 300;
    int maxChangedFiles = 10;

    // Behavior
    bool dryRunDefault = true;
    bool aiCompletionEnabled = true;
    bool debugLogging = false;
    bool agentDebug = false;

    // AI Completion trigger
    int completionMinChars = 3;   // trigger after N chars of a word
    int completionDelayMs = 500;  // debounce delay in milliseconds
    QString completionModel;      // model for completion (empty = same as agent model)
    QString completionThinkingLevel = QStringLiteral("off");    // off|low|medium|high
    QString completionReasoningEffort = QStringLiteral("off");  // off|low|medium|high

    void load();
    void save() const;
};

// Global singleton accessor
Settings &settings();

class ModelCatalog : public QObject
{
    Q_OBJECT
public:
    explicit ModelCatalog(QObject *parent = nullptr);

    QStringList copilotModels() const;
    void setCopilotModels(const QStringList &models);

    static QStringList defaultCopilotModels();

signals:
    void copilotModelsChanged(const QStringList &models);

private:
    QStringList m_copilotModels;
};

ModelCatalog &modelCatalog();

}  // namespace qcai2
