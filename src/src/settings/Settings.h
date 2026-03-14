#pragma once

#include <qtmcp/ServerDefinition.h>

#include <QObject>
#include <QString>
#include <QStringList>

namespace qcai2
{

/**
 * Stores persistent plugin settings loaded from QSettings.
 */
struct Settings
{
    /** Selected backend identifier. */
    QString provider = QStringLiteral("openai");

    /** Base URL for OpenAI-compatible providers. */
    QString baseUrl = QStringLiteral("https://api.openai.com");

    /** API key for the active remote provider. */
    QString apiKey;

    /** Default model used for agent requests. */
    QString modelName = QStringLiteral("gpt-5.2");

    /** Provider reasoning effort: off, low, medium, or high. */
    QString reasoningEffort = QStringLiteral("medium");

    /** Provider thinking level: off, low, medium, or high. */
    QString thinkingLevel = QStringLiteral("medium");

    /** Sampling temperature for agent requests. */
    double temperature = 0.2;

    /** Maximum token budget for agent responses. */
    int maxTokens = 4096;

    /** Base URL for the local HTTP provider. */
    QString localBaseUrl = QStringLiteral("http://localhost:8080");

    /** Endpoint path appended to the local provider base URL. */
    QString localEndpointPath = QStringLiteral("/v1/chat/completions");

    /** Sends local requests in simplified prompt mode when true. */
    bool localSimpleMode = false;

    /** Extra HTTP headers for the local provider. */
    QString localCustomHeaders;

    /** Base URL for the Ollama server. */
    QString ollamaBaseUrl = QStringLiteral("http://localhost:11434");

    /** Default Ollama model name. */
    QString ollamaModel = QStringLiteral("llama4");

    /** Optional cached Copilot token. */
    QString copilotToken;

    /** Default model exposed by the Copilot sidecar. */
    QString copilotModel = QStringLiteral("gpt-4o");

    /** Path to the Node.js executable used to launch the sidecar. */
    QString copilotNodePath;

    /** Optional explicit path to the Copilot sidecar script. */
    QString copilotSidecarPath;

    /** Maximum agent loop iterations allowed per request. */
    int maxIterations = 8;

    /** Maximum number of tool calls allowed per request. */
    int maxToolCalls = 25;

    /** Maximum allowed changed lines before approval is required. */
    int maxDiffLines = 300;

    /** Maximum allowed changed files before approval is required. */
    int maxChangedFiles = 10;

    /** Starts the agent in dry-run mode by default when true. */
    bool dryRunDefault = true;

    /** Enables AI completion features inside the editor. */
    bool aiCompletionEnabled = true;

    /** Enables plugin debug logging. */
    bool debugLogging = false;

    /** Shows raw agent JSON payloads in chat when true. */
    bool agentDebug = false;

    /** Minimum identifier length that triggers auto-completion. */
    int completionMinChars = 3;

    /** Debounce delay for automatic completion requests, in milliseconds. */
    int completionDelayMs = 500;

    /** Optional model override for code completion requests. */
    QString completionModel;

    /** Completion thinking level: off, low, medium, or high. */
    QString completionThinkingLevel = QStringLiteral("off");

    /** Completion reasoning effort: off, low, medium, or high. */
    QString completionReasoningEffort = QStringLiteral("off");

    /** Globally configured MCP servers keyed by logical server name. */
    qtmcp::ServerDefinitions mcpServers;

    /**
     * Loads settings from QSettings.
     */
    void load();

    /**
     * Saves settings to QSettings.
     */
    void save() const;

};

struct McpServerConnectionState
{
    QString state;
    QString message;
};

using McpServerConnectionStates = QMap<QString, McpServerConnectionState>;

/**
 * Returns the process-wide settings singleton.
 * @return Shared settings instance.
 */
Settings &settings();

McpServerConnectionStates loadMcpServerConnectionStates(QString *error = nullptr);
bool saveMcpServerConnectionStates(const McpServerConnectionStates &states,
                                   QString *error = nullptr);

/**
 * Tracks user-visible model choices for provider-specific UIs.
 */
class ModelCatalog : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a model catalog.
     * @param parent Owning QObject.
     */
    explicit ModelCatalog(QObject *parent = nullptr);

    /**
     * Returns the current Copilot model list.
     * @return Normalized model names shown in the settings UI.
     */
    QStringList copilotModels() const;

    /**
     * Replaces the Copilot model list.
     * @param models Candidate model names.
     */
    void setCopilotModels(const QStringList &models);

    /**
     * Returns the built-in default Copilot models.
     * @return Default model names used when no runtime list is available.
     */
    static QStringList defaultCopilotModels();

signals:
    /**
     * Emitted after the Copilot model list changes.
     * @param models Normalized model names exposed to the UI.
     */
    void copilotModelsChanged(const QStringList &models);

private:
    /**
     * Current normalized Copilot model list.
     */
    QStringList m_copilotModels;

};

/**
 * Returns the process-wide model catalog singleton.
 * @return Shared model catalog instance.
 */
ModelCatalog &modelCatalog();

}  // namespace qcai2
