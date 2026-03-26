/*! @file
    @brief Implements the Qt Creator settings page for the plugin.
*/

#include "settings_page.h"
#include "../../qcai2tr.h"
#include "../util/logger.h"
#include "../util/web_tools.h"
#include "../vector_search/text_embedder.h"
#include "../vector_search/vector_indexing_manager.h"
#include "../vector_search/vector_search_service.h"
#include "mcp_servers_widget.h"
#include "settings.h"
#include "settings_double_spin_box.h"
#include "settings_spin_box.h"
#include "ui_settings_widget.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <utils/filepath.h>
#include <utils/guiutils.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include <memory>

namespace qcai2
{

namespace
{

void repopulateEditableCombo(QComboBox *combo, const QStringList &items,
                             const QString &selectedText)
{
    QStringList uniqueItems;
    uniqueItems.reserve(items.size() + 1);

    for (const QString &item : items)
    {
        const QString trimmed = item.trimmed();
        if (trimmed.isEmpty() || uniqueItems.contains(trimmed))
        {
            continue;
        }
        uniqueItems.append(trimmed);
    }

    const QString selected = selectedText.trimmed();
    if (!selected.isEmpty() && !uniqueItems.contains(selected))
    {
        uniqueItems.append(selected);
    }

    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItems(uniqueItems);
    combo->setCurrentText(selected);
}

void populateEffortCombo(QComboBox *combo)
{
    combo->addItem(tr_t::tr("Off"), QStringLiteral("off"));
    combo->addItem(tr_t::tr("Low"), QStringLiteral("low"));
    combo->addItem(tr_t::tr("Medium"), QStringLiteral("medium"));
    combo->addItem(tr_t::tr("High"), QStringLiteral("high"));
}

void selectEffortValue(QComboBox *combo, const QString &value, int fallbackIndex)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallbackIndex);
}

void browse_for_existing_file(QWidget *parent, QLineEdit *line_edit, const QString &title)
{
    const QString selected_path =
        QFileDialog::getOpenFileName(parent, title, line_edit->text().trimmed());
    if (selected_path.isEmpty() == false)
    {
        line_edit->setText(selected_path);
    }
}

QStringList ollama_model_presets()
{
    return {
        QStringLiteral("llama4"),
        QStringLiteral("llama3.3"),
        QStringLiteral("llama3.1"),
        QStringLiteral("deepseek-r1"),
        QStringLiteral("deepseek-coder-v2"),
        QStringLiteral("qwen2.5-coder"),
        QStringLiteral("mistral"),
        QStringLiteral("mixtral"),
        QStringLiteral("gemma2"),
        QStringLiteral("phi4"),
        QStringLiteral("codellama"),
    };
}

QStringList generic_completion_models()
{
    return {
        QStringLiteral("gpt-5.4-mini"),      QStringLiteral("gpt-5.3-codex"),
        QStringLiteral("gpt-4.1-nano"),      QStringLiteral("gpt-4.1-mini"),
        QStringLiteral("gpt-5-mini"),        QStringLiteral("gpt-5.1-codex-mini"),
        QStringLiteral("gpt-5.1-codex"),     QStringLiteral("gpt-5.2-codex"),
        QStringLiteral("claude-haiku-4.5"),  QStringLiteral("claude-sonnet-4.6"),
        QStringLiteral("gemini-3-flash"),    QStringLiteral("gemini-2.5-flash-lite"),
        QStringLiteral("gemini-2.5-flash"),  QStringLiteral("deepseek-coder"),
        QStringLiteral("qwen2.5-coder-32b"), QStringLiteral("codellama"),
    };
}

}  // namespace

#if QCAI2_FEATURE_QDRANT_ENABLE
constexpr bool k_qdrant_support_enabled = true;
#else
constexpr bool k_qdrant_support_enabled = false;
#endif

class settings_widget_t : public Core::IOptionsPageWidget
{
    struct snapshot_t
    {
        QString provider;
        QString base_url;
        QString api_key;
        QString model_name;
        QString reasoning_effort;
        QString thinking_level;
        double temperature = 0;
        int max_tokens = 0;
        QString copilot_model;
        QString copilot_node_path;
        QString copilot_sidecar_path;
        int copilot_completion_timeout_sec = 0;
        QString local_base_url;
        QString local_endpoint_path;
        bool local_simple_mode = false;
        QString local_custom_headers;
        QString ollama_base_url;
        QString ollama_model;
        int max_iterations = 0;
        int max_tool_calls = 0;
        int max_diff_lines = 0;
        int max_changed_files = 0;
        bool dry_run_default = false;
        bool ai_completion_enabled = false;
        bool detailed_completion_logging = false;
        int completion_min_chars = 0;
        int completion_delay_ms = 0;
        QString completion_provider;
        QString completion_model;
        bool completion_send_thinking = false;
        QString completion_thinking_level;
        bool completion_send_reasoning = false;
        QString completion_reasoning_effort;
        bool vector_search_enabled = false;
        QString vector_search_provider;
        QString qdrant_url;
        QString qdrant_api_key;
        QString qdrant_collection_name;
        bool qdrant_auto_create_collection = false;
        QString qdrant_ca_certificate_file;
        QString qdrant_client_certificate_file;
        QString qdrant_client_key_file;
        bool qdrant_allow_self_signed_server_certificate = false;
        int qdrant_timeout_sec = 0;
        int vector_search_max_indexing_threads = 0;
        bool debug_logging = false;
        bool detailed_request_logging = false;
        QString system_prompt;
        bool load_agents_md = false;
        bool load_github_copilot_instructions = false;
        bool load_claude_md = false;
        bool load_gemini_md = false;
        bool load_github_instructions_dir = false;
        bool web_tools_enabled = false;
        QString web_search_provider;
        QString web_search_endpoint;
        QString web_search_api_key;
        int web_search_max_results = 0;
        int web_request_timeout_sec = 0;
        int web_fetch_max_chars = 0;
        bool inline_diff_refinement_enabled = false;
        bool agent_debug = false;
        qtmcp::server_definitions_t mcp_servers;
        bool auto_compact_enabled = false;
        int auto_compact_threshold_tokens = 0;

        friend bool operator==(const snapshot_t &, const snapshot_t &) = default;
    };

public:
    settings_widget_t()
    {
        this->ui = std::make_unique<Ui::settings_widget_t>();
        this->ui->setupUi(this);

        auto &s = settings();
        const settings_t defaults;

        this->providerCombo = this->ui->providerCombo;
        this->baseUrlCombo = this->ui->baseUrlCombo;
        this->apiKeyEdit = this->ui->apiKeyEdit;
        this->modelCombo = this->ui->modelCombo;
        this->reasoningCombo = this->ui->reasoningCombo;
        this->thinkingCombo = this->ui->thinkingCombo;
        this->tempSpin = this->ui->tempSpin;
        this->maxTokensSpin = this->ui->maxTokensSpin;
        this->copilotModelCombo = this->ui->copilotModelCombo;
        this->copilotNodeEdit = this->ui->copilotNodeEdit;
        this->copilotSidecarEdit = this->ui->copilotSidecarEdit;
        this->copilotCompletionTimeoutSpin = this->ui->copilotCompletionTimeoutSpin;
        this->localUrlEdit = this->ui->localUrlEdit;
        this->localEndpointEdit = this->ui->localEndpointEdit;
        this->localSimpleCheck = this->ui->localSimpleCheck;
        this->localHeadersEdit = this->ui->localHeadersEdit;
        this->ollamaUrlEdit = this->ui->ollamaUrlEdit;
        this->ollamaModelCombo = this->ui->ollamaModelCombo;
        this->maxIterSpin = this->ui->maxIterSpin;
        this->maxToolsSpin = this->ui->maxToolsSpin;
        this->maxDiffSpin = this->ui->maxDiffSpin;
        this->maxFilesSpin = this->ui->maxFilesSpin;
        this->dryRunCheck = this->ui->dryRunCheck;
        this->autoCompactCheck = this->ui->autoCompactCheck;
        this->autoCompactThresholdSpin = this->ui->autoCompactThresholdSpin;
        this->aiCompletionCheck = this->ui->aiCompletionCheck;
        this->detailedCompletionLoggingCheck = this->ui->detailedCompletionLoggingCheck;
        this->completionMinCharsSpin = this->ui->completionMinCharsSpin;
        this->completionDelayMsSpin = this->ui->completionDelayMsSpin;
        this->completionProviderCombo = this->ui->completionProviderCombo;
        this->completionModelCombo = this->ui->completionModelCombo;
        this->completionSendThinkingCheck = this->ui->completionSendThinkingCheck;
        this->completionThinkingCombo = this->ui->completionThinkingCombo;
        this->completionSendReasoningCheck = this->ui->completionSendReasoningCheck;
        this->completionReasoningCombo = this->ui->completionReasoningCombo;
        this->vectorSearchEnabledCheck = this->ui->vectorSearchEnabledCheck;
        this->vectorSearchProviderCombo = this->ui->vectorSearchProviderCombo;
        this->vectorSearchStatusLabel = this->ui->vectorSearchStatusLabel;
        this->qdrantGroup = this->ui->qdrantGroup;
        this->qdrantUrlEdit = this->ui->qdrantUrlEdit;
        this->qdrantApiKeyEdit = this->ui->qdrantApiKeyEdit;
        this->qdrantCollectionEdit = this->ui->qdrantCollectionEdit;
        this->qdrantAutoCreateCollectionCheck = this->ui->qdrantAutoCreateCollectionCheck;
        this->qdrantCaCertificateEdit = this->ui->qdrantCaCertificateEdit;
        this->qdrantCaCertificateBrowseButton = this->ui->qdrantCaCertificateBrowseButton;
        this->qdrantClientCertificateEdit = this->ui->qdrantClientCertificateEdit;
        this->qdrantClientCertificateBrowseButton = this->ui->qdrantClientCertificateBrowseButton;
        this->qdrantClientKeyEdit = this->ui->qdrantClientKeyEdit;
        this->qdrantClientKeyBrowseButton = this->ui->qdrantClientKeyBrowseButton;
        this->qdrantAllowSelfSignedCheck = this->ui->qdrantAllowSelfSignedCheck;
        this->qdrantTimeoutSpin = this->ui->qdrantTimeoutSpin;
        this->vectorSearchMaxIndexingThreadsSpin = this->ui->vectorSearchMaxIndexingThreadsSpin;
        this->qdrantTestConnectionButton = this->ui->qdrantTestConnectionButton;
        this->debugLoggingCheck = this->ui->debugLoggingCheck;
        this->detailedRequestLoggingCheck = this->ui->detailedRequestLoggingCheck;
        this->systemPromptEdit = this->ui->systemPromptEdit;
        this->loadAgentsMdCheck = this->ui->loadAgentsMdCheck;
        this->loadGithubCopilotInstructionsCheck = this->ui->loadGithubCopilotInstructionsCheck;
        this->loadClaudeMdCheck = this->ui->loadClaudeMdCheck;
        this->loadGeminiMdCheck = this->ui->loadGeminiMdCheck;
        this->loadGithubInstructionsDirCheck = this->ui->loadGithubInstructionsDirCheck;
        this->webToolsEnabledCheck = this->ui->webToolsEnabledCheck;
        this->webSearchProviderCombo = this->ui->webSearchProviderCombo;
        this->webSearchEndpointEdit = this->ui->webSearchEndpointEdit;
        this->webSearchApiKeyEdit = this->ui->webSearchApiKeyEdit;
        this->webSearchMaxResultsSpin = this->ui->webSearchMaxResultsSpin;
        this->webRequestTimeoutSpin = this->ui->webRequestTimeoutSpin;
        this->webFetchMaxCharsSpin = this->ui->webFetchMaxCharsSpin;
        this->webToolsStatusLabel = this->ui->webToolsStatusLabel;
        this->inlineDiffRefinementCheck = this->ui->inlineDiffRefinementCheck;
        this->agentDebugCheck = this->ui->agentDebugCheck;
        this->tempSpin->set_default_value(defaults.temperature);
        this->maxTokensSpin->set_default_value(defaults.max_tokens);
        this->copilotCompletionTimeoutSpin->set_default_value(
            defaults.copilot_completion_timeout_sec);
        this->maxIterSpin->set_default_value(defaults.max_iterations);
        this->maxToolsSpin->set_default_value(defaults.max_tool_calls);
        this->maxDiffSpin->set_default_value(defaults.max_diff_lines);
        this->maxFilesSpin->set_default_value(defaults.max_changed_files);
        this->autoCompactThresholdSpin->set_default_value(defaults.auto_compact_threshold_tokens);
        this->completionMinCharsSpin->set_default_value(defaults.completion_min_chars);
        this->completionDelayMsSpin->set_default_value(defaults.completion_delay_ms);
        this->qdrantTimeoutSpin->set_default_value(defaults.qdrant_timeout_sec);
        this->vectorSearchMaxIndexingThreadsSpin->set_default_value(
            defaults.vector_search_max_indexing_threads);
        this->webSearchMaxResultsSpin->set_default_value(defaults.web_search_max_results);
        this->webRequestTimeoutSpin->set_default_value(defaults.web_request_timeout_sec);
        this->webFetchMaxCharsSpin->set_default_value(defaults.web_fetch_max_chars);
        this->mcpServersWidget = new mcp_servers_widget_t(this);
        this->mcpServersWidget->set_servers(s.mcp_servers);
        const int vector_search_tab_index =
            this->ui->settingsTabs->indexOf(this->ui->vectorSearchTab);
        this->ui->settingsTabs->insertTab(
            vector_search_tab_index >= 0 ? vector_search_tab_index + 1 : 1, this->mcpServersWidget,
            tr_t::tr("MCP Servers"));

        // Provider selection
        this->providerCombo->addItem(tr_t::tr("OpenAI-Compatible"), QStringLiteral("openai"));
        this->providerCombo->addItem(tr_t::tr("Anthropic API"), QStringLiteral("anthropic"));
        this->providerCombo->addItem(tr_t::tr("GitHub Copilot"), QStringLiteral("copilot"));
        this->providerCombo->addItem(tr_t::tr("Local HTTP"), QStringLiteral("local"));
        this->providerCombo->addItem(tr_t::tr("Ollama (Local)"), QStringLiteral("ollama"));
        int idx = this->providerCombo->findData(s.provider);
        if (((idx >= 0) == true))
        {
            this->providerCombo->setCurrentIndex(idx);
        }

        // OpenAI settings
        // Base URL combo: editable, with popular providers
        this->baseUrlCombo->addItems({
            QStringLiteral("https://api.openai.com"),
            QStringLiteral("https://api.anthropic.com"),
            QStringLiteral("https://generativelanguage.googleapis.com"),
            QStringLiteral("https://api.x.ai"),
            QStringLiteral("https://api.deepseek.com"),
            QStringLiteral("https://api.mistral.ai"),
            QStringLiteral("https://api.groq.com/openai"),
            QStringLiteral("https://openrouter.ai/api"),
            QStringLiteral("https://api.together.xyz"),
            QStringLiteral("https://api.fireworks.ai/inference"),
            QStringLiteral("http://localhost:1234"),
            QStringLiteral("http://localhost:8080"),
        });
        this->baseUrlCombo->setCurrentText(s.base_url);
        this->apiKeyEdit->setText(s.api_key);

        // Model combo: editable, with presets
        this->modelCombo->addItems({
            // OpenAI — GPT-5 family
            QStringLiteral("gpt-5.4"),
            QStringLiteral("gpt-5.4-mini"),
            QStringLiteral("gpt-5.3-codex"),
            QStringLiteral("gpt-5.2-codex"),
            QStringLiteral("gpt-5.2"),
            QStringLiteral("gpt-5.1-codex-max"),
            QStringLiteral("gpt-5.1"),
            QStringLiteral("gpt-5.1-codex"),
            QStringLiteral("gpt-5.1-codex-mini"),
            QStringLiteral("gpt-5-mini"),
            // OpenAI — GPT-4.1
            QStringLiteral("gpt-4.1"),
            QStringLiteral("gpt-4.1-mini"),
            QStringLiteral("gpt-4.1-nano"),
            // OpenAI — O-series reasoning
            QStringLiteral("o3"),
            QStringLiteral("o4-mini"),
            // Anthropic Claude
            QStringLiteral("claude-opus-4.6"),
            QStringLiteral("claude-opus-4.6-fast"),
            QStringLiteral("claude-opus-4.5"),
            QStringLiteral("claude-sonnet-4.6"),
            QStringLiteral("claude-sonnet-4.5"),
            QStringLiteral("claude-sonnet-4"),
            QStringLiteral("claude-haiku-4.5"),
            // Google Gemini
            QStringLiteral("gemini-3-pro-preview"),
            QStringLiteral("gemini-3-flash"),
            QStringLiteral("gemini-2.5-pro"),
            QStringLiteral("gemini-2.5-flash"),
            QStringLiteral("gemini-2.5-flash-lite"),
            // DeepSeek
            QStringLiteral("deepseek-r1"),
            QStringLiteral("deepseek-chat"),
            QStringLiteral("deepseek-coder"),
            // Meta Llama
            QStringLiteral("llama-4"),
            QStringLiteral("llama-3.3-70b"),
            // Mistral
            QStringLiteral("mistral-large-latest"),
            QStringLiteral("mistral-small-latest"),
            // Qwen
            QStringLiteral("qwen2.5-coder-32b"),
            // xAI Grok
            QStringLiteral("grok-4.1"),
            QStringLiteral("grok-4.1-fast"),
            QStringLiteral("grok-4"),
            QStringLiteral("grok-3"),
            QStringLiteral("grok-3-mini"),
            QStringLiteral("grok-3-fast"),
        });
        this->modelCombo->setCurrentText(s.model_name);

        populateEffortCombo(this->reasoningCombo);
        selectEffortValue(this->reasoningCombo, s.reasoning_effort, 2);

        populateEffortCombo(this->thinkingCombo);
        selectEffortValue(this->thinkingCombo, s.thinking_level, 2);

        this->tempSpin->set_value(s.temperature);

        this->maxTokensSpin->set_value(s.max_tokens);

        // GitHub Copilot (sidecar)
        repopulateEditableCombo(this->copilotModelCombo, model_catalog().copilot_models(),
                                s.copilot_model);
        connect(&model_catalog(), &model_catalog_t::copilot_models_changed, this,
                [this](const QStringList &models) {
                    repopulateEditableCombo(this->copilotModelCombo, models,
                                            this->copilotModelCombo->currentText());
                    this->refreshCompletionModelOptions();
                });

        this->copilotNodeEdit->setText(s.copilot_node_path);
        this->copilotSidecarEdit->setText(s.copilot_sidecar_path);
        this->copilotCompletionTimeoutSpin->set_value(s.copilot_completion_timeout_sec);

        // Local provider settings
        this->localUrlEdit->setText(s.local_base_url);
        this->localEndpointEdit->setText(s.local_endpoint_path);
        this->localSimpleCheck->setChecked(s.local_simple_mode);
        this->localHeadersEdit->setText(s.local_custom_headers);

        // Ollama
        this->ollamaUrlEdit->setText(s.ollama_base_url);
        this->ollamaModelCombo->addItems(ollama_model_presets());
        this->ollamaModelCombo->setCurrentText(s.ollama_model);

        // Safety
        this->maxIterSpin->set_value(s.max_iterations);
        this->maxToolsSpin->set_value(s.max_tool_calls);
        this->maxDiffSpin->set_value(s.max_diff_lines);
        this->maxFilesSpin->set_value(s.max_changed_files);

        this->dryRunCheck->setChecked(s.dry_run_default);
        this->autoCompactCheck->setChecked(s.auto_compact_enabled);
        this->autoCompactThresholdSpin->set_value(s.auto_compact_threshold_tokens);

        this->aiCompletionCheck->setChecked(s.ai_completion_enabled);
        this->detailedCompletionLoggingCheck->setChecked(s.detailed_completion_logging);

        this->completionMinCharsSpin->set_value(s.completion_min_chars);

        this->completionDelayMsSpin->set_value(s.completion_delay_ms);

        this->completionProviderCombo->addItem(tr_t::tr("Same as agent"), QString());
        this->completionProviderCombo->addItem(tr_t::tr("OpenAI-Compatible"),
                                               QStringLiteral("openai"));
        this->completionProviderCombo->addItem(tr_t::tr("Anthropic API"),
                                               QStringLiteral("anthropic"));
        this->completionProviderCombo->addItem(tr_t::tr("GitHub Copilot"),
                                               QStringLiteral("copilot"));
        this->completionProviderCombo->addItem(tr_t::tr("Local HTTP"), QStringLiteral("local"));
        this->completionProviderCombo->addItem(tr_t::tr("Ollama (Local)"),
                                               QStringLiteral("ollama"));
        const int completion_provider_index =
            this->completionProviderCombo->findData(s.completion_provider);
        this->completionProviderCombo->setCurrentIndex(
            completion_provider_index >= 0 ? completion_provider_index : 0);

        if (this->completionModelCombo->lineEdit() != nullptr)
        {
            this->completionModelCombo->lineEdit()->setPlaceholderText(
                tr_t::tr("(Same as selected provider default model)"));
        }
        this->refreshCompletionModelOptions();
        this->completionModelCombo->setCurrentText(s.completion_model.trimmed());

        this->completionSendThinkingCheck->setChecked(s.completion_send_thinking);
        populateEffortCombo(this->completionThinkingCombo);
        selectEffortValue(this->completionThinkingCombo, s.completion_thinking_level, 0);

        this->completionSendReasoningCheck->setChecked(s.completion_send_reasoning);
        populateEffortCombo(this->completionReasoningCombo);
        selectEffortValue(this->completionReasoningCombo, s.completion_reasoning_effort, 0);
        this->refreshCompletionUiState();

        this->vectorSearchEnabledCheck->setChecked(s.vector_search_enabled);
        if (k_qdrant_support_enabled == true)
        {
            this->vectorSearchProviderCombo->addItem(tr_t::tr("Qdrant"), QStringLiteral("qdrant"));
            this->vectorSearchStatusLabel->clear();
            this->vectorSearchStatusLabel->hide();
        }
        else
        {
            this->vectorSearchProviderCombo->addItem(tr_t::tr("Qdrant (not built)"),
                                                     QStringLiteral("qdrant"));
            this->vectorSearchProviderCombo->setEnabled(false);
            this->vectorSearchStatusLabel->setText(tr_t::tr(
                "Qdrant support was disabled at build time (`FEATURE_QDRANT_ENABLE=OFF`)."));
            this->vectorSearchStatusLabel->show();
        }
        const int vector_provider_index =
            this->vectorSearchProviderCombo->findData(s.vector_search_provider);
        this->vectorSearchProviderCombo->setCurrentIndex(
            vector_provider_index >= 0 ? vector_provider_index : 0);
        this->qdrantUrlEdit->setText(s.qdrant_url);
        this->qdrantApiKeyEdit->setText(s.qdrant_api_key);
        this->qdrantCollectionEdit->setText(s.qdrant_collection_name);
        this->qdrantAutoCreateCollectionCheck->setChecked(s.qdrant_auto_create_collection);
        this->qdrantCaCertificateEdit->setText(s.qdrant_ca_certificate_file);
        this->qdrantClientCertificateEdit->setText(s.qdrant_client_certificate_file);
        this->qdrantClientKeyEdit->setText(s.qdrant_client_key_file);
        this->qdrantAllowSelfSignedCheck->setChecked(
            s.qdrant_allow_self_signed_server_certificate);
        this->qdrantTimeoutSpin->set_value(s.qdrant_timeout_sec);
        this->vectorSearchMaxIndexingThreadsSpin->set_value(s.vector_search_max_indexing_threads);

        QObject::connect(
            this->qdrantCaCertificateBrowseButton, &QPushButton::clicked, this, [this]() {
                browse_for_existing_file(this, this->qdrantCaCertificateEdit,
                                         tr_t::tr("Select Qdrant CA certificate file"));
            });
        QObject::connect(
            this->qdrantClientCertificateBrowseButton, &QPushButton::clicked, this, [this]() {
                browse_for_existing_file(this, this->qdrantClientCertificateEdit,
                                         tr_t::tr("Select Qdrant client certificate file"));
            });
        QObject::connect(this->qdrantClientKeyBrowseButton, &QPushButton::clicked, this, [this]() {
            browse_for_existing_file(this, this->qdrantClientKeyEdit,
                                     tr_t::tr("Select Qdrant client key file"));
        });

        this->refreshVectorSearchUiState();
        this->refreshVectorSearchStatusFromService();
        connect(this->vectorSearchEnabledCheck, &QCheckBox::toggled, this,
                [this]() { this->refreshVectorSearchUiState(); });
        connect(this->vectorSearchProviderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { this->refreshVectorSearchUiState(); });
        connect(this->qdrantTestConnectionButton, &QPushButton::clicked, this,
                [this]() { this->testVectorSearchConnection(); });
        connect(&vector_search_service(), &vector_search_service_t::availability_changed, this,
                [this](bool, const QString &) { this->refreshVectorSearchStatusFromService(); });

        this->debugLoggingCheck->setChecked(s.debug_logging);
        this->detailedRequestLoggingCheck->setChecked(s.detailed_request_logging);
        this->systemPromptEdit->setPlainText(s.system_prompt);
        this->loadAgentsMdCheck->setChecked(s.load_agents_md);
        this->loadGithubCopilotInstructionsCheck->setChecked(s.load_github_copilot_instructions);
        this->loadClaudeMdCheck->setChecked(s.load_claude_md);
        this->loadGeminiMdCheck->setChecked(s.load_gemini_md);
        this->loadGithubInstructionsDirCheck->setChecked(s.load_github_instructions_dir);
        this->webToolsEnabledCheck->setChecked(s.web_tools_enabled);
        this->webSearchProviderCombo->addItem(tr_t::tr("DuckDuckGo HTML"),
                                              QStringLiteral("duckduckgo"));
        this->webSearchProviderCombo->addItem(tr_t::tr("Perplexity API"),
                                              QStringLiteral("perplexity"));
        this->webSearchProviderCombo->addItem(tr_t::tr("Brave Search API"),
                                              QStringLiteral("brave"));
        this->webSearchProviderCombo->addItem(tr_t::tr("Serper.dev"), QStringLiteral("serper"));
        const int web_provider_index =
            this->webSearchProviderCombo->findData(s.web_search_provider);
        this->webSearchProviderCombo->setCurrentIndex(web_provider_index >= 0 ? web_provider_index
                                                                              : 0);
        this->webSearchEndpointEdit->setText(
            s.web_search_endpoint.trimmed().isEmpty() == true
                ? default_web_search_endpoint(
                      this->webSearchProviderCombo->currentData().toString())
                : s.web_search_endpoint.trimmed());
        this->webSearchApiKeyEdit->setText(s.web_search_api_key);
        this->webSearchMaxResultsSpin->set_value(s.web_search_max_results);
        this->webRequestTimeoutSpin->set_value(s.web_request_timeout_sec);
        this->webFetchMaxCharsSpin->set_value(s.web_fetch_max_chars);
        this->webSearchProviderCombo->setProperty(
            "previousProvider", this->webSearchProviderCombo->currentData().toString());
        connect(this->webToolsEnabledCheck, &QCheckBox::toggled, this,
                [this]() { this->refreshWebToolsUiState(); });
        connect(this->webSearchProviderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    const QString previous_provider =
                        this->webSearchProviderCombo->property("previousProvider").toString();
                    const QString current_provider =
                        this->webSearchProviderCombo->currentData().toString();
                    const QString current_endpoint = this->webSearchEndpointEdit->text().trimmed();
                    if (current_endpoint.isEmpty() == true ||
                        current_endpoint == default_web_search_endpoint(previous_provider))
                    {
                        this->webSearchEndpointEdit->setText(
                            default_web_search_endpoint(current_provider));
                    }
                    this->webSearchProviderCombo->setProperty("previousProvider",
                                                              current_provider);
                    this->refreshWebToolsUiState();
                });
        this->refreshWebToolsUiState();
        this->inlineDiffRefinementCheck->setChecked(s.inline_diff_refinement_enabled);

        this->agentDebugCheck->setChecked(s.agent_debug);

        this->snapshot = this->captureSnapshot();

        // Notify the framework when any widget value changes so it can poll isDirty().
        using Utils::installCheckSettingsDirtyTrigger;
        installCheckSettingsDirtyTrigger(this->providerCombo);
        installCheckSettingsDirtyTrigger(this->baseUrlCombo);
        installCheckSettingsDirtyTrigger(this->apiKeyEdit);
        installCheckSettingsDirtyTrigger(this->modelCombo);
        installCheckSettingsDirtyTrigger(this->reasoningCombo);
        installCheckSettingsDirtyTrigger(this->thinkingCombo);
        connect(this->maxTokensSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->copilotModelCombo);
        installCheckSettingsDirtyTrigger(this->copilotNodeEdit);
        installCheckSettingsDirtyTrigger(this->copilotSidecarEdit);
        connect(this->copilotCompletionTimeoutSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->localUrlEdit);
        installCheckSettingsDirtyTrigger(this->localEndpointEdit);
        installCheckSettingsDirtyTrigger(this->localSimpleCheck);
        installCheckSettingsDirtyTrigger(this->localHeadersEdit);
        installCheckSettingsDirtyTrigger(this->ollamaUrlEdit);
        installCheckSettingsDirtyTrigger(this->ollamaModelCombo);
        connect(this->maxIterSpin, &settings_spin_box_t::value_changed, Utils::checkSettingsDirty);
        connect(this->maxToolsSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->maxDiffSpin, &settings_spin_box_t::value_changed, Utils::checkSettingsDirty);
        connect(this->maxFilesSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->dryRunCheck);
        installCheckSettingsDirtyTrigger(this->autoCompactCheck);
        connect(this->autoCompactThresholdSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->autoCompactCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->aiCompletionCheck);
        installCheckSettingsDirtyTrigger(this->detailedCompletionLoggingCheck);
        connect(this->completionMinCharsSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->completionDelayMsSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->completionProviderCombo);
        installCheckSettingsDirtyTrigger(this->completionModelCombo);
        installCheckSettingsDirtyTrigger(this->completionSendThinkingCheck);
        installCheckSettingsDirtyTrigger(this->completionThinkingCombo);
        installCheckSettingsDirtyTrigger(this->completionSendReasoningCheck);
        installCheckSettingsDirtyTrigger(this->completionReasoningCombo);
        installCheckSettingsDirtyTrigger(this->vectorSearchEnabledCheck);
        installCheckSettingsDirtyTrigger(this->vectorSearchProviderCombo);
        installCheckSettingsDirtyTrigger(this->qdrantUrlEdit);
        installCheckSettingsDirtyTrigger(this->qdrantApiKeyEdit);
        installCheckSettingsDirtyTrigger(this->qdrantCollectionEdit);
        installCheckSettingsDirtyTrigger(this->qdrantAutoCreateCollectionCheck);
        installCheckSettingsDirtyTrigger(this->qdrantCaCertificateEdit);
        installCheckSettingsDirtyTrigger(this->qdrantClientCertificateEdit);
        installCheckSettingsDirtyTrigger(this->qdrantClientKeyEdit);
        installCheckSettingsDirtyTrigger(this->qdrantAllowSelfSignedCheck);
        connect(this->qdrantTimeoutSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->vectorSearchMaxIndexingThreadsSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->debugLoggingCheck);
        installCheckSettingsDirtyTrigger(this->detailedRequestLoggingCheck);
        installCheckSettingsDirtyTrigger(this->loadAgentsMdCheck);
        installCheckSettingsDirtyTrigger(this->loadGithubCopilotInstructionsCheck);
        installCheckSettingsDirtyTrigger(this->loadClaudeMdCheck);
        installCheckSettingsDirtyTrigger(this->loadGeminiMdCheck);
        installCheckSettingsDirtyTrigger(this->loadGithubInstructionsDirCheck);
        installCheckSettingsDirtyTrigger(this->webToolsEnabledCheck);
        installCheckSettingsDirtyTrigger(this->webSearchProviderCombo);
        installCheckSettingsDirtyTrigger(this->webSearchEndpointEdit);
        installCheckSettingsDirtyTrigger(this->webSearchApiKeyEdit);
        connect(this->webSearchMaxResultsSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->webRequestTimeoutSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->webFetchMaxCharsSpin, &settings_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->inlineDiffRefinementCheck);
        installCheckSettingsDirtyTrigger(this->agentDebugCheck);
        connect(this->tempSpin, &settings_double_spin_box_t::value_changed,
                Utils::checkSettingsDirty);
        connect(this->providerCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this]() { this->refreshCompletionModelOptions(); });
        connect(this->completionProviderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this]() { this->refreshCompletionModelOptions(); });
        connect(this->completionModelCombo, &QComboBox::currentTextChanged,
                Utils::checkSettingsDirty);
        connect(this->aiCompletionCheck, &QCheckBox::toggled, this,
                [this]() { this->refreshCompletionUiState(); });
        connect(this->completionSendThinkingCheck, &QCheckBox::toggled, this,
                [this]() { this->refreshCompletionUiState(); });
        connect(this->completionSendReasoningCheck, &QCheckBox::toggled, this,
                [this]() { this->refreshCompletionUiState(); });
        connect(this->detailedCompletionLoggingCheck, &QCheckBox::toggled,
                Utils::checkSettingsDirty);
        connect(this->detailedRequestLoggingCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        connect(this->systemPromptEdit, &QPlainTextEdit::textChanged, Utils::checkSettingsDirty);
        connect(this->webToolsEnabledCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        connect(this->inlineDiffRefinementCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        connect(this->vectorSearchEnabledCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        connect(this->mcpServersWidget, &mcp_servers_widget_t::changed, Utils::checkSettingsDirty);
    }

    bool isDirty() const override
    {
        return this->captureSnapshot() != this->snapshot;
    }

    void apply() override
    {
        settings_t candidate = this->settingsFromUi();
        candidate.debug_logging = this->debugLoggingCheck->isChecked();
        candidate.detailed_request_logging = this->detailedRequestLoggingCheck->isChecked();
        candidate.system_prompt = this->systemPromptEdit->toPlainText().trimmed();
        candidate.load_agents_md = this->loadAgentsMdCheck->isChecked();
        candidate.load_github_copilot_instructions =
            this->loadGithubCopilotInstructionsCheck->isChecked();
        candidate.load_claude_md = this->loadClaudeMdCheck->isChecked();
        candidate.load_gemini_md = this->loadGeminiMdCheck->isChecked();
        candidate.load_github_instructions_dir = this->loadGithubInstructionsDirCheck->isChecked();
        candidate.web_tools_enabled = this->webToolsEnabledCheck->isChecked();
        candidate.web_search_provider = this->webSearchProviderCombo->currentData().toString();
        candidate.web_search_endpoint = this->webSearchEndpointEdit->text().trimmed();
        candidate.web_search_api_key = this->webSearchApiKeyEdit->text();
        candidate.web_search_max_results = this->webSearchMaxResultsSpin->value();
        candidate.web_request_timeout_sec = this->webRequestTimeoutSpin->value();
        candidate.web_fetch_max_chars = this->webFetchMaxCharsSpin->value();
        candidate.inline_diff_refinement_enabled = this->inlineDiffRefinementCheck->isChecked();
        candidate.agent_debug = this->agentDebugCheck->isChecked();

        auto &s = settings();
        s = candidate;
        s.save();

        logger_t::instance().set_enabled(s.debug_logging);
        vector_search_service().apply_settings(s);
        this->refreshVectorSearchStatusFromService();

        QString error;
        if ((k_qdrant_support_enabled == true) && (s.vector_search_enabled == true) &&
            ((vector_search_service().verify_connection(&error) == false) ||
             (vector_search_service().ensure_collection(text_embedder_t::k_embedding_dimensions,
                                                        &error) == false)))
        {
            QMessageBox::warning(
                this, tr_t::tr("Vector Search"),
                tr_t::tr("Qdrant server is not available: %1\n\n"
                         "Vector search functionality is disabled until the connection succeeds.")
                    .arg(error));
            this->refreshVectorSearchStatusFromService();
        }

        vector_indexing_manager().refresh_from_settings();

        this->snapshot = this->captureSnapshot();
    }

    void cancel() override
    {
        this->restoreSnapshot(this->snapshot);
        vector_search_service().apply_settings(settings());
        this->refreshVectorSearchStatusFromService();
    }

private:
    settings_t settingsFromUi() const
    {
        settings_t s = settings();
        s.provider = this->providerCombo->currentData().toString();
        s.base_url = this->baseUrlCombo->currentText();
        s.api_key = this->apiKeyEdit->text();
        s.model_name = this->modelCombo->currentText();
        s.reasoning_effort = this->reasoningCombo->currentData().toString();
        s.thinking_level = this->thinkingCombo->currentData().toString();
        s.temperature = this->tempSpin->value();
        s.max_tokens = this->maxTokensSpin->value();
        s.copilot_model = this->copilotModelCombo->currentText();
        s.copilot_node_path = this->copilotNodeEdit->text();
        s.copilot_sidecar_path = this->copilotSidecarEdit->text();
        s.copilot_completion_timeout_sec = this->copilotCompletionTimeoutSpin->value();
        s.local_base_url = this->localUrlEdit->text();
        s.local_endpoint_path = this->localEndpointEdit->text();
        s.local_simple_mode = this->localSimpleCheck->isChecked();
        s.local_custom_headers = this->localHeadersEdit->text();
        s.ollama_base_url = this->ollamaUrlEdit->text();
        s.ollama_model = this->ollamaModelCombo->currentText();
        s.max_iterations = this->maxIterSpin->value();
        s.max_tool_calls = this->maxToolsSpin->value();
        s.max_diff_lines = this->maxDiffSpin->value();
        s.max_changed_files = this->maxFilesSpin->value();
        s.dry_run_default = this->dryRunCheck->isChecked();
        s.auto_compact_enabled = this->autoCompactCheck->isChecked();
        s.auto_compact_threshold_tokens = this->autoCompactThresholdSpin->value();
        s.ai_completion_enabled = this->aiCompletionCheck->isChecked();
        s.detailed_completion_logging = this->detailedCompletionLoggingCheck->isChecked();
        s.completion_min_chars = this->completionMinCharsSpin->value();
        s.completion_delay_ms = this->completionDelayMsSpin->value();
        s.completion_provider = this->completionProviderCombo->currentData().toString();
        s.completion_model = this->completionModelCombo->currentText().trimmed();
        s.completion_send_thinking = this->completionSendThinkingCheck->isChecked();
        s.completion_thinking_level = this->completionThinkingCombo->currentData().toString();
        s.completion_send_reasoning = this->completionSendReasoningCheck->isChecked();
        s.completion_reasoning_effort = this->completionReasoningCombo->currentData().toString();
        s.vector_search_enabled = this->vectorSearchEnabledCheck->isChecked();
        s.vector_search_provider = this->vectorSearchProviderCombo->currentData().toString();
        s.qdrant_url = this->qdrantUrlEdit->text().trimmed();
        s.qdrant_api_key = this->qdrantApiKeyEdit->text();
        s.qdrant_collection_name = this->qdrantCollectionEdit->text().trimmed();
        s.qdrant_auto_create_collection = this->qdrantAutoCreateCollectionCheck->isChecked();
        s.qdrant_ca_certificate_file = this->qdrantCaCertificateEdit->text().trimmed();
        s.qdrant_client_certificate_file = this->qdrantClientCertificateEdit->text().trimmed();
        s.qdrant_client_key_file = this->qdrantClientKeyEdit->text().trimmed();
        s.qdrant_allow_self_signed_server_certificate =
            this->qdrantAllowSelfSignedCheck->isChecked();
        s.qdrant_timeout_sec = this->qdrantTimeoutSpin->value();
        s.vector_search_max_indexing_threads = this->vectorSearchMaxIndexingThreadsSpin->value();
        s.debug_logging = this->debugLoggingCheck->isChecked();
        s.detailed_request_logging = this->detailedRequestLoggingCheck->isChecked();
        s.system_prompt = this->systemPromptEdit->toPlainText().trimmed();
        s.load_agents_md = this->loadAgentsMdCheck->isChecked();
        s.load_github_copilot_instructions = this->loadGithubCopilotInstructionsCheck->isChecked();
        s.load_claude_md = this->loadClaudeMdCheck->isChecked();
        s.load_gemini_md = this->loadGeminiMdCheck->isChecked();
        s.load_github_instructions_dir = this->loadGithubInstructionsDirCheck->isChecked();
        s.web_tools_enabled = this->webToolsEnabledCheck->isChecked();
        s.web_search_provider = this->webSearchProviderCombo->currentData().toString();
        s.web_search_endpoint = this->webSearchEndpointEdit->text().trimmed();
        s.web_search_api_key = this->webSearchApiKeyEdit->text();
        s.web_search_max_results = this->webSearchMaxResultsSpin->value();
        s.web_request_timeout_sec = this->webRequestTimeoutSpin->value();
        s.web_fetch_max_chars = this->webFetchMaxCharsSpin->value();
        s.inline_diff_refinement_enabled = this->inlineDiffRefinementCheck->isChecked();
        s.agent_debug = this->agentDebugCheck->isChecked();
        s.mcp_servers = this->mcpServersWidget->servers();
        return s;
    }

    QString effectiveCompletionProviderForUi() const
    {
        const QString completion_provider =
            this->completionProviderCombo->currentData().toString();
        return completion_provider.isEmpty() == false
                   ? completion_provider
                   : this->providerCombo->currentData().toString();
    }

    void refreshCompletionModelOptions()
    {
        const QString provider_id = this->effectiveCompletionProviderForUi();
        const QString current_text = this->completionModelCombo->currentText().trimmed();
        QStringList items;
        if (provider_id == QStringLiteral("copilot"))
        {
            items = model_catalog().copilot_models();
        }
        else if (provider_id == QStringLiteral("ollama"))
        {
            items = ollama_model_presets();
        }
        else
        {
            items = generic_completion_models();
        }

        repopulateEditableCombo(this->completionModelCombo, items, current_text);
    }

    void refreshCompletionUiState()
    {
        const bool completion_enabled = this->aiCompletionCheck->isChecked();
        this->detailedCompletionLoggingCheck->setEnabled(completion_enabled);
        this->completionProviderCombo->setEnabled(completion_enabled);
        this->completionModelCombo->setEnabled(completion_enabled);
        this->completionMinCharsSpin->setEnabled(completion_enabled);
        this->completionDelayMsSpin->setEnabled(completion_enabled);
        this->completionSendThinkingCheck->setEnabled(completion_enabled);
        this->completionThinkingCombo->setEnabled(completion_enabled &&
                                                  this->completionSendThinkingCheck->isChecked());
        this->completionSendReasoningCheck->setEnabled(completion_enabled);
        this->completionReasoningCombo->setEnabled(
            completion_enabled && this->completionSendReasoningCheck->isChecked());
    }

    void refreshWebToolsUiState()
    {
        const bool enabled = this->webToolsEnabledCheck->isChecked();
        const QString provider = this->webSearchProviderCombo->currentData().toString();
        const bool provider_requires_api_key = provider == QStringLiteral("serper") ||
                                               provider == QStringLiteral("brave") ||
                                               provider == QStringLiteral("perplexity");

        this->webSearchProviderCombo->setEnabled(enabled);
        this->webSearchEndpointEdit->setEnabled(enabled);
        this->webSearchApiKeyEdit->setEnabled(enabled && provider_requires_api_key);
        this->webSearchMaxResultsSpin->setEnabled(enabled);
        this->webRequestTimeoutSpin->setEnabled(enabled);
        this->webFetchMaxCharsSpin->setEnabled(enabled);
        this->webToolsStatusLabel->setText(
            enabled == false
                ? tr_t::tr("Web tools are disabled.")
                : tr_t::tr("Available to all agent providers, including GitHub Copilot. %1")
                      .arg(provider_requires_api_key == true
                               ? (provider == QStringLiteral("perplexity")
                                      ? tr_t::tr("Perplexity API requires a configured API key.")
                                  : provider == QStringLiteral("brave")
                                      ? tr_t::tr("Brave Search API requires a configured API key.")
                                      : tr_t::tr("Serper.dev requires a configured API key."))
                               : tr_t::tr("DuckDuckGo HTML works without an API key.")));
    }

    void refreshVectorSearchUiState()
    {
        const bool provider_available = k_qdrant_support_enabled;
        const bool enabled = this->vectorSearchEnabledCheck->isChecked();
        const bool qdrant_selected =
            this->vectorSearchProviderCombo->currentData().toString() == QStringLiteral("qdrant");
        this->vectorSearchProviderCombo->setEnabled(provider_available);
        this->qdrantGroup->setEnabled(enabled && provider_available && qdrant_selected);
        this->qdrantTestConnectionButton->setEnabled(enabled && provider_available &&
                                                     qdrant_selected);
    }

    void refreshVectorSearchStatusFromService()
    {
        const auto &service = vector_search_service();

        if (k_qdrant_support_enabled == false)
        {
            this->vectorSearchStatusLabel->setText(tr_t::tr(
                "Qdrant support was disabled at build time (`FEATURE_QDRANT_ENABLE=OFF`)."));
            this->vectorSearchStatusLabel->show();
            return;
        }

        if (this->vectorSearchEnabledCheck->isChecked() == false)
        {
            this->vectorSearchStatusLabel->setText(tr_t::tr("Vector search is disabled."));
            this->vectorSearchStatusLabel->show();
            return;
        }

        if (service.is_available() == true)
        {
            this->vectorSearchStatusLabel->setText(tr_t::tr("Qdrant connection is available."));
        }
        else if (service.last_error().isEmpty() == false)
        {
            this->vectorSearchStatusLabel->setText(
                tr_t::tr("Qdrant connection failed: %1").arg(service.last_error()));
        }
        else
        {
            this->vectorSearchStatusLabel->setText(
                tr_t::tr("Qdrant connection has not been verified yet."));
        }
        this->vectorSearchStatusLabel->show();
    }

    void testVectorSearchConnection()
    {
        const settings_t candidate = this->settingsFromUi();
        vector_search_service().apply_settings(candidate);
        if (k_qdrant_support_enabled == false)
        {
            this->refreshVectorSearchStatusFromService();
            return;
        }
        QString error;
        if (vector_search_service().verify_connection(&error) == true)
        {
            if (vector_search_service().ensure_collection(text_embedder_t::k_embedding_dimensions,
                                                          &error) == false)
            {
                this->refreshVectorSearchStatusFromService();
                QMessageBox::warning(
                    this, tr_t::tr("Vector Search"),
                    tr_t::tr(
                        "Qdrant server is not available: %1\n\n"
                        "Vector search functionality is disabled until the connection succeeds.")
                        .arg(error));
                return;
            }
            this->refreshVectorSearchStatusFromService();
            QMessageBox::information(this, tr_t::tr("Vector Search"),
                                     tr_t::tr("Qdrant connection succeeded."));
            return;
        }

        this->refreshVectorSearchStatusFromService();
        QMessageBox::warning(
            this, tr_t::tr("Vector Search"),
            tr_t::tr("Qdrant server is not available: %1\n\n"
                     "Vector search functionality is disabled until the connection succeeds.")
                .arg(error));
    }

    snapshot_t captureSnapshot() const
    {
        return {
            this->providerCombo->currentData().toString(),
            this->baseUrlCombo->currentText(),
            this->apiKeyEdit->text(),
            this->modelCombo->currentText(),
            this->reasoningCombo->currentData().toString(),
            this->thinkingCombo->currentData().toString(),
            this->tempSpin->value(),
            this->maxTokensSpin->value(),
            this->copilotModelCombo->currentText(),
            this->copilotNodeEdit->text(),
            this->copilotSidecarEdit->text(),
            this->copilotCompletionTimeoutSpin->value(),
            this->localUrlEdit->text(),
            this->localEndpointEdit->text(),
            this->localSimpleCheck->isChecked(),
            this->localHeadersEdit->text(),
            this->ollamaUrlEdit->text(),
            this->ollamaModelCombo->currentText(),
            this->maxIterSpin->value(),
            this->maxToolsSpin->value(),
            this->maxDiffSpin->value(),
            this->maxFilesSpin->value(),
            this->dryRunCheck->isChecked(),
            this->aiCompletionCheck->isChecked(),
            this->detailedCompletionLoggingCheck->isChecked(),
            this->completionMinCharsSpin->value(),
            this->completionDelayMsSpin->value(),
            this->completionProviderCombo->currentData().toString(),
            this->completionModelCombo->currentText().trimmed(),
            this->completionSendThinkingCheck->isChecked(),
            this->completionThinkingCombo->currentData().toString(),
            this->completionSendReasoningCheck->isChecked(),
            this->completionReasoningCombo->currentData().toString(),
            this->vectorSearchEnabledCheck->isChecked(),
            this->vectorSearchProviderCombo->currentData().toString(),
            this->qdrantUrlEdit->text().trimmed(),
            this->qdrantApiKeyEdit->text(),
            this->qdrantCollectionEdit->text().trimmed(),
            this->qdrantAutoCreateCollectionCheck->isChecked(),
            this->qdrantCaCertificateEdit->text().trimmed(),
            this->qdrantClientCertificateEdit->text().trimmed(),
            this->qdrantClientKeyEdit->text().trimmed(),
            this->qdrantAllowSelfSignedCheck->isChecked(),
            this->qdrantTimeoutSpin->value(),
            this->vectorSearchMaxIndexingThreadsSpin->value(),
            this->debugLoggingCheck->isChecked(),
            this->detailedRequestLoggingCheck->isChecked(),
            this->systemPromptEdit->toPlainText().trimmed(),
            this->loadAgentsMdCheck->isChecked(),
            this->loadGithubCopilotInstructionsCheck->isChecked(),
            this->loadClaudeMdCheck->isChecked(),
            this->loadGeminiMdCheck->isChecked(),
            this->loadGithubInstructionsDirCheck->isChecked(),
            this->webToolsEnabledCheck->isChecked(),
            this->webSearchProviderCombo->currentData().toString(),
            this->webSearchEndpointEdit->text().trimmed(),
            this->webSearchApiKeyEdit->text(),
            this->webSearchMaxResultsSpin->value(),
            this->webRequestTimeoutSpin->value(),
            this->webFetchMaxCharsSpin->value(),
            this->inlineDiffRefinementCheck->isChecked(),
            this->agentDebugCheck->isChecked(),
            this->mcpServersWidget->servers(),
            this->autoCompactCheck->isChecked(),
            this->autoCompactThresholdSpin->value(),
        };
    }

    void restoreSnapshot(const snapshot_t &snap)
    {
        const int provIdx = this->providerCombo->findData(snap.provider);
        if (provIdx >= 0)
            this->providerCombo->setCurrentIndex(provIdx);

        this->baseUrlCombo->setCurrentText(snap.base_url);
        this->apiKeyEdit->setText(snap.api_key);
        this->modelCombo->setCurrentText(snap.model_name);
        selectEffortValue(this->reasoningCombo, snap.reasoning_effort, 2);
        selectEffortValue(this->thinkingCombo, snap.thinking_level, 2);
        this->tempSpin->set_value(snap.temperature);
        this->maxTokensSpin->set_value(snap.max_tokens);

        this->copilotModelCombo->setCurrentText(snap.copilot_model);
        this->copilotNodeEdit->setText(snap.copilot_node_path);
        this->copilotSidecarEdit->setText(snap.copilot_sidecar_path);
        this->copilotCompletionTimeoutSpin->set_value(snap.copilot_completion_timeout_sec);

        this->localUrlEdit->setText(snap.local_base_url);
        this->localEndpointEdit->setText(snap.local_endpoint_path);
        this->localSimpleCheck->setChecked(snap.local_simple_mode);
        this->localHeadersEdit->setText(snap.local_custom_headers);

        this->ollamaUrlEdit->setText(snap.ollama_base_url);
        this->ollamaModelCombo->setCurrentText(snap.ollama_model);

        this->maxIterSpin->set_value(snap.max_iterations);
        this->maxToolsSpin->set_value(snap.max_tool_calls);
        this->maxDiffSpin->set_value(snap.max_diff_lines);
        this->maxFilesSpin->set_value(snap.max_changed_files);

        this->dryRunCheck->setChecked(snap.dry_run_default);
        this->autoCompactCheck->setChecked(snap.auto_compact_enabled);
        this->autoCompactThresholdSpin->set_value(snap.auto_compact_threshold_tokens);
        this->aiCompletionCheck->setChecked(snap.ai_completion_enabled);
        this->detailedCompletionLoggingCheck->setChecked(snap.detailed_completion_logging);
        this->completionMinCharsSpin->set_value(snap.completion_min_chars);
        this->completionDelayMsSpin->set_value(snap.completion_delay_ms);
        const int completion_provider_index =
            this->completionProviderCombo->findData(snap.completion_provider);
        this->completionProviderCombo->setCurrentIndex(
            completion_provider_index >= 0 ? completion_provider_index : 0);
        this->refreshCompletionModelOptions();

        this->completionModelCombo->setCurrentText(snap.completion_model.trimmed());

        this->completionSendThinkingCheck->setChecked(snap.completion_send_thinking);
        selectEffortValue(this->completionThinkingCombo, snap.completion_thinking_level, 0);
        this->completionSendReasoningCheck->setChecked(snap.completion_send_reasoning);
        selectEffortValue(this->completionReasoningCombo, snap.completion_reasoning_effort, 0);
        this->refreshCompletionUiState();

        this->vectorSearchEnabledCheck->setChecked(snap.vector_search_enabled);
        const int vector_provider_index =
            this->vectorSearchProviderCombo->findData(snap.vector_search_provider);
        this->vectorSearchProviderCombo->setCurrentIndex(
            vector_provider_index >= 0 ? vector_provider_index : 0);
        this->qdrantUrlEdit->setText(snap.qdrant_url);
        this->qdrantApiKeyEdit->setText(snap.qdrant_api_key);
        this->qdrantCollectionEdit->setText(snap.qdrant_collection_name);
        this->qdrantAutoCreateCollectionCheck->setChecked(snap.qdrant_auto_create_collection);
        this->qdrantCaCertificateEdit->setText(snap.qdrant_ca_certificate_file);
        this->qdrantClientCertificateEdit->setText(snap.qdrant_client_certificate_file);
        this->qdrantClientKeyEdit->setText(snap.qdrant_client_key_file);
        this->qdrantAllowSelfSignedCheck->setChecked(
            snap.qdrant_allow_self_signed_server_certificate);
        this->qdrantTimeoutSpin->set_value(snap.qdrant_timeout_sec);
        this->vectorSearchMaxIndexingThreadsSpin->set_value(
            snap.vector_search_max_indexing_threads);
        this->refreshVectorSearchUiState();

        this->debugLoggingCheck->setChecked(snap.debug_logging);
        this->detailedRequestLoggingCheck->setChecked(snap.detailed_request_logging);
        this->systemPromptEdit->setPlainText(snap.system_prompt);
        this->loadAgentsMdCheck->setChecked(snap.load_agents_md);
        this->loadGithubCopilotInstructionsCheck->setChecked(
            snap.load_github_copilot_instructions);
        this->loadClaudeMdCheck->setChecked(snap.load_claude_md);
        this->loadGeminiMdCheck->setChecked(snap.load_gemini_md);
        this->loadGithubInstructionsDirCheck->setChecked(snap.load_github_instructions_dir);
        this->webToolsEnabledCheck->setChecked(snap.web_tools_enabled);
        const int web_provider_index =
            this->webSearchProviderCombo->findData(snap.web_search_provider);
        this->webSearchProviderCombo->setCurrentIndex(web_provider_index >= 0 ? web_provider_index
                                                                              : 0);
        this->webSearchEndpointEdit->setText(snap.web_search_endpoint);
        this->webSearchApiKeyEdit->setText(snap.web_search_api_key);
        this->webSearchMaxResultsSpin->set_value(snap.web_search_max_results);
        this->webRequestTimeoutSpin->set_value(snap.web_request_timeout_sec);
        this->webFetchMaxCharsSpin->set_value(snap.web_fetch_max_chars);
        this->webSearchProviderCombo->setProperty(
            "previousProvider", this->webSearchProviderCombo->currentData().toString());
        this->refreshWebToolsUiState();
        this->inlineDiffRefinementCheck->setChecked(snap.inline_diff_refinement_enabled);
        this->agentDebugCheck->setChecked(snap.agent_debug);
        this->mcpServersWidget->set_servers(snap.mcp_servers);
        this->refreshVectorSearchStatusFromService();
    }

    snapshot_t snapshot;
    std::unique_ptr<Ui::settings_widget_t> ui;
    QComboBox *providerCombo;
    QComboBox *baseUrlCombo;
    QLineEdit *apiKeyEdit;
    QComboBox *modelCombo;
    QComboBox *reasoningCombo;
    QComboBox *thinkingCombo;
    settings_double_spin_box_t *tempSpin;
    settings_spin_box_t *maxTokensSpin;
    QLineEdit *copilotNodeEdit, *copilotSidecarEdit;
    QComboBox *copilotModelCombo;
    settings_spin_box_t *copilotCompletionTimeoutSpin;
    QLineEdit *localUrlEdit, *localEndpointEdit, *localHeadersEdit;
    QCheckBox *localSimpleCheck;
    QLineEdit *ollamaUrlEdit;
    QComboBox *ollamaModelCombo;
    settings_spin_box_t *maxIterSpin, *maxToolsSpin, *maxDiffSpin, *maxFilesSpin;
    QCheckBox *dryRunCheck;
    QCheckBox *autoCompactCheck;
    settings_spin_box_t *autoCompactThresholdSpin;
    QCheckBox *aiCompletionCheck;
    QCheckBox *detailedCompletionLoggingCheck;
    settings_spin_box_t *completionMinCharsSpin;
    settings_spin_box_t *completionDelayMsSpin;
    QComboBox *completionProviderCombo;
    QComboBox *completionModelCombo;
    QCheckBox *completionSendThinkingCheck;
    QComboBox *completionThinkingCombo;
    QCheckBox *completionSendReasoningCheck;
    QComboBox *completionReasoningCombo;
    QCheckBox *vectorSearchEnabledCheck;
    QComboBox *vectorSearchProviderCombo;
    QLabel *vectorSearchStatusLabel;
    QGroupBox *qdrantGroup;
    QLineEdit *qdrantUrlEdit;
    QLineEdit *qdrantApiKeyEdit;
    QLineEdit *qdrantCollectionEdit;
    QCheckBox *qdrantAutoCreateCollectionCheck;
    QLineEdit *qdrantCaCertificateEdit;
    QPushButton *qdrantCaCertificateBrowseButton;
    QLineEdit *qdrantClientCertificateEdit;
    QPushButton *qdrantClientCertificateBrowseButton;
    QLineEdit *qdrantClientKeyEdit;
    QPushButton *qdrantClientKeyBrowseButton;
    QCheckBox *qdrantAllowSelfSignedCheck;
    settings_spin_box_t *qdrantTimeoutSpin;
    settings_spin_box_t *vectorSearchMaxIndexingThreadsSpin;
    QPushButton *qdrantTestConnectionButton;
    QCheckBox *debugLoggingCheck;
    QCheckBox *detailedRequestLoggingCheck;
    QPlainTextEdit *systemPromptEdit;
    QCheckBox *loadAgentsMdCheck;
    QCheckBox *loadGithubCopilotInstructionsCheck;
    QCheckBox *loadClaudeMdCheck;
    QCheckBox *loadGeminiMdCheck;
    QCheckBox *loadGithubInstructionsDirCheck;
    QCheckBox *webToolsEnabledCheck;
    QComboBox *webSearchProviderCombo;
    QLineEdit *webSearchEndpointEdit;
    QLineEdit *webSearchApiKeyEdit;
    settings_spin_box_t *webSearchMaxResultsSpin;
    settings_spin_box_t *webRequestTimeoutSpin;
    settings_spin_box_t *webFetchMaxCharsSpin;
    QLabel *webToolsStatusLabel;
    QCheckBox *inlineDiffRefinementCheck;
    QCheckBox *agentDebugCheck;
    mcp_servers_widget_t *mcpServersWidget = nullptr;
};

settings_page_t::settings_page_t()
{
    setId("qcai2.settings_t");
    setDisplayName(tr_t::tr("Qcai2"));
    setCategory("qcai2");
    IOptionsPage::registerCategory(
        "qcai2", tr_t::tr("Qcai2"),
        Utils::FilePath::fromString(QStringLiteral(":/qcai2/ai-agent-icon.svg")));

    setWidgetCreator([]() -> Core::IOptionsPageWidget * { return new settings_widget_t; });
}

}  // namespace qcai2
