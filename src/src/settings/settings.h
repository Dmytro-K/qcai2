#pragma once

#include <qtmcp/server_definition.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

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
    QString model_name = QStringLiteral("gpt-5.4");

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
    QString copilot_model = QStringLiteral("gpt-5.4");

    /** Path to the Node.js executable used to launch the sidecar. */
    QString copilot_node_path;

    /** Optional explicit path to the Copilot sidecar script. */
    QString copilot_sidecar_path;

    /** Maximum wait time for Copilot `sendAndWait()` / `session.idle`, in seconds; 0 disables it.
     */
    int copilot_completion_timeout_sec = 1200;

    /** Maximum agent loop iterations allowed per request. Set to 0 for unlimited iterations. */
    int max_iterations = 8;

    /** Maximum number of tool calls allowed per request. Set to 0 for unlimited tool calls. */
    int max_tool_calls = 25;

    /** Maximum allowed changed lines before approval is required. Set to 0 for no line limit. */
    int max_diff_lines = 300;

    /** Maximum allowed changed files before approval is required. Set to 0 for no file limit. */
    int max_changed_files = 10;

    /** Starts the agent in dry-run mode by default when true. */
    bool dry_run_default = true;

    /** Automatically triggers /compact before the next request when the
     *  previous request's input-token count exceeds the threshold. */
    bool auto_compact_enabled = false;

    /** Input-token threshold for auto-compact (ignored when auto_compact_enabled is false). */
    int auto_compact_threshold_tokens = 8000;

    /** Enables AI completion features inside the editor. */
    bool ai_completion_enabled = true;

    /** Appends detailed per-completion Markdown logs into the project `.qcai2/logs` folder. */
    bool detailed_completion_logging = false;

    /** Enables plugin debug logging. */
    bool debug_logging = false;

    /** Appends detailed per-request Markdown logs into the project `.qcai2/logs` folder. */
    bool detailed_request_logging = false;

    /** Optional custom system prompt prepended to agent and ask requests. */
    QString system_prompt;

    /** Auto-loads project-root `AGENTS.md` into system instructions when true. */
    bool load_agents_md = true;

    /** Auto-loads project-root `.github/copilot-instructions.md` when true. */
    bool load_github_copilot_instructions = true;

    /** Auto-loads project-root `CLAUDE.md` into system instructions when true. */
    bool load_claude_md = true;

    /** Auto-loads project-root `GEMINI.md` into system instructions when true. */
    bool load_gemini_md = true;

    /** Auto-loads recursive instruction files from `.github/instructions/` when true. */
    bool load_github_instructions_dir = true;

    /** Enables web_search and web_fetch tools when true. */
    bool web_tools_enabled = false;

    /** Selected backend identifier for web search requests. */
    QString web_search_provider = QStringLiteral("duckduckgo");

    /** Configured web search HTTP endpoint. */
    QString web_search_endpoint = QStringLiteral("https://html.duckduckgo.com/html/");

    /** Optional API key for web search backends that require one. */
    QString web_search_api_key;

    /** Default number of web search results returned to the agent. */
    int web_search_max_results = 5;

    /** Shared request timeout for web tools, in seconds. */
    int web_request_timeout_sec = 20;

    /** Maximum number of fetched characters returned by web_fetch. */
    int web_fetch_max_chars = 20000;

    /** Allows rejected inline diff hunks to trigger a follow-up refinement round. */
    bool inline_diff_refinement_enabled = false;

    /** Shows raw agent JSON payloads in chat when true. */
    bool agent_debug = false;

    /** Minimum identifier length that triggers auto-completion. */
    int completion_min_chars = 3;

    /** Debounce delay for automatic completion requests, in milliseconds. */
    int completion_delay_ms = 500;

    /** Optional provider override for code completion requests; empty follows the agent provider.
     */
    QString completion_provider;

    /** Optional model override for code completion requests. */
    QString completion_model;

    /** Sends completion thinking instructions when true. */
    bool completion_send_thinking = false;

    /** Completion thinking level: off, low, medium, or high. */
    QString completion_thinking_level = QStringLiteral("off");

    /** Sends completion reasoning effort when true. */
    bool completion_send_reasoning = true;

    /** Completion reasoning effort: off, low, medium, or high. */
    QString completion_reasoning_effort = QStringLiteral("off");

    /** Enables vector search features when true. */
    bool vector_search_enabled = false;

    /** Selected vector search backend identifier. */
    QString vector_search_provider = QStringLiteral("qdrant");

    /** Base URL of the Qdrant HTTP API. */
    QString qdrant_url = QStringLiteral("http://localhost:6333");

    /** Optional Qdrant API key. */
    QString qdrant_api_key;

    /** Default Qdrant collection used by vector search. */
    QString qdrant_collection_name = QStringLiteral("qcai2-symbols");

    /** Automatically creates the configured Qdrant collection when it is missing. */
    bool qdrant_auto_create_collection = true;

    /** Optional CA certificate file used for Qdrant TLS validation. */
    QString qdrant_ca_certificate_file;

    /** Optional client certificate file used for Qdrant mTLS. */
    QString qdrant_client_certificate_file;

    /** Optional client private key file used for Qdrant mTLS. */
    QString qdrant_client_key_file;

    /** Allows self-signed server certificates for Qdrant when true. */
    bool qdrant_allow_self_signed_server_certificate = false;

    /** Qdrant request timeout in seconds. */
    int qdrant_timeout_sec = 30;

    /** Maximum number of worker threads used for full-workspace vector indexing. */
    int vector_search_max_indexing_threads = qMax(1, QThread::idealThreadCount());

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
