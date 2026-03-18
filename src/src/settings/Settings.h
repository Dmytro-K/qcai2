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
struct settings_t
{
    /** Selected backend identifier. */
    QString provider = QStringLiteral("openai");

    /** Base URL for OpenAI-compatible providers. */
    QString base_url = QStringLiteral("https://api.openai.com");

    /** API key for the active remote provider. */
    QString api_key;

    /** Default model used for agent requests. */
    QString model_name = QStringLiteral("gpt-5.2");

    /** Provider reasoning effort: off, low, medium, or high. */
    QString reasoning_effort = QStringLiteral("medium");

    /** Provider thinking level: off, low, medium, or high. */
    QString thinking_level = QStringLiteral("medium");

    /** Sampling temperature for agent requests. */
    double temperature = 0.2;

    /** Maximum token budget for agent responses. */
    int max_tokens = 4096;

    /** Base URL for the local HTTP provider. */
    QString local_base_url = QStringLiteral("http://localhost:8080");

    /** Endpoint path appended to the local provider base URL. */
    QString local_endpoint_path = QStringLiteral("/v1/chat/completions");

    /** Sends local requests in simplified prompt mode when true. */
    bool local_simple_mode = false;

    /** Extra HTTP headers for the local provider. */
    QString local_custom_headers;

    /** Base URL for the Ollama server. */
    QString ollama_base_url = QStringLiteral("http://localhost:11434");

    /** Default Ollama model name. */
    QString ollama_model = QStringLiteral("llama4");

    /** Optional cached Copilot token. */
    QString copilot_token;

    /** Default model exposed by the Copilot sidecar. */
    QString copilot_model = QStringLiteral("gpt-4o");

    /** Path to the Node.js executable used to launch the sidecar. */
    QString copilot_node_path;

    /** Optional explicit path to the Copilot sidecar script. */
    QString copilot_sidecar_path;

    /** Maximum Copilot completion wait time in seconds; 0 disables the timeout. */
    int copilot_completion_timeout_sec = 300;

    /** Maximum agent loop iterations allowed per request. */
    int max_iterations = 8;

    /** Maximum number of tool calls allowed per request. */
    int max_tool_calls = 25;

    /** Maximum allowed changed lines before approval is required. */
    int max_diff_lines = 300;

    /** Maximum allowed changed files before approval is required. */
    int max_changed_files = 10;

    /** Starts the agent in dry-run mode by default when true. */
    bool dry_run_default = true;

    /** Enables AI completion features inside the editor. */
    bool ai_completion_enabled = true;

    /** Enables plugin debug logging. */
    bool debug_logging = false;

    /** Shows raw agent JSON payloads in chat when true. */
    bool agent_debug = false;

    /** Minimum identifier length that triggers auto-completion. */
    int completion_min_chars = 3;

    /** Debounce delay for automatic completion requests, in milliseconds. */
    int completion_delay_ms = 500;

    /** Optional model override for code completion requests. */
    QString completion_model;

    /** Completion thinking level: off, low, medium, or high. */
    QString completion_thinking_level = QStringLiteral("off");

    /** Completion reasoning effort: off, low, medium, or high. */
    QString completion_reasoning_effort = QStringLiteral("off");

    /** Globally configured MCP servers keyed by logical server name. */
    qtmcp::server_definitions_t mcp_servers;

    /**
     * Loads settings from QSettings.
     */
    void load();

    /**
     * Saves settings to QSettings.
     */
    void save() const;
};

struct mcp_server_connection_state_t
{
    QString state;
    QString message;
};

using mcp_server_connection_states_t = QMap<QString, mcp_server_connection_state_t>;

/**
 * Returns the process-wide settings singleton.
 * @return Shared settings instance.
 */
settings_t &settings();

mcp_server_connection_states_t load_mcp_server_connection_states(QString *error = nullptr);
bool save_mcp_server_connection_states(const mcp_server_connection_states_t &states,
                                       QString *error = nullptr);

/**
 * Tracks user-visible model choices for provider-specific UIs.
 */
class model_catalog_t : public QObject
{
    Q_OBJECT
public:
    /**
     * Creates a model catalog.
     * @param parent Owning QObject.
     */
    explicit model_catalog_t(QObject *parent = nullptr);

    /**
     * Returns the current Copilot model list.
     * @return Normalized model names shown in the settings UI.
     */
    QStringList copilot_models() const;

    /**
     * Replaces the Copilot model list.
     * @param models Candidate model names.
     */
    void set_copilot_models(const QStringList &models);

    /**
     * Returns the built-in default Copilot models.
     * @return Default model names used when no runtime list is available.
     */
    static QStringList default_copilot_models();

signals:
    /**
     * Emitted after the Copilot model list changes.
     * @param models Normalized model names exposed to the UI.
     */
    void copilot_models_changed(const QStringList &models);

private:
    /**
     * Current normalized Copilot model list.
     */
    QStringList available_copilot_models;
};

/**
 * Returns the process-wide model catalog singleton.
 * @return Shared model catalog instance.
 */
model_catalog_t &model_catalog();

}  // namespace qcai2
