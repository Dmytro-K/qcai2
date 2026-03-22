/*! @file
    @brief Implements the Qt Creator settings page for the plugin.
*/

#include "settings_page.h"
#include "../../qcai2tr.h"
#include "../util/logger.h"
#include "../vector_search/text_embedder.h"
#include "../vector_search/vector_indexing_manager.h"
#include "../vector_search/vector_search_service.h"
#include "mcp_servers_widget.h"
#include "settings.h"
#include "ui_settings_widget.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <utils/filepath.h>
#include <utils/guiutils.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
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
#include <QSpinBox>
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
        int completion_min_chars = 0;
        int completion_delay_ms = 0;
        QString completion_model;
        QString completion_thinking_level;
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
        this->completionMinCharsSpin = this->ui->completionMinCharsSpin;
        this->completionDelayMsSpin = this->ui->completionDelayMsSpin;
        this->completionModelCombo = this->ui->completionModelCombo;
        this->completionThinkingCombo = this->ui->completionThinkingCombo;
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
        this->inlineDiffRefinementCheck = this->ui->inlineDiffRefinementCheck;
        this->agentDebugCheck = this->ui->agentDebugCheck;
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

        this->tempSpin->setValue(s.temperature);

        this->maxTokensSpin->setValue(s.max_tokens);

        // GitHub Copilot (sidecar)
        repopulateEditableCombo(this->copilotModelCombo, model_catalog().copilot_models(),
                                s.copilot_model);
        connect(&model_catalog(), &model_catalog_t::copilot_models_changed, this,
                [this](const QStringList &models) {
                    repopulateEditableCombo(this->copilotModelCombo, models,
                                            this->copilotModelCombo->currentText());
                });

        this->copilotNodeEdit->setText(s.copilot_node_path);
        this->copilotSidecarEdit->setText(s.copilot_sidecar_path);
        this->copilotCompletionTimeoutSpin->setValue(s.copilot_completion_timeout_sec);

        // Local provider settings
        this->localUrlEdit->setText(s.local_base_url);
        this->localEndpointEdit->setText(s.local_endpoint_path);
        this->localSimpleCheck->setChecked(s.local_simple_mode);
        this->localHeadersEdit->setText(s.local_custom_headers);

        // Ollama
        this->ollamaUrlEdit->setText(s.ollama_base_url);
        this->ollamaModelCombo->addItems({
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
        });
        this->ollamaModelCombo->setCurrentText(s.ollama_model);

        // Safety
        this->maxIterSpin->setValue(s.max_iterations);
        this->maxToolsSpin->setValue(s.max_tool_calls);
        this->maxDiffSpin->setValue(s.max_diff_lines);
        this->maxFilesSpin->setValue(s.max_changed_files);

        this->dryRunCheck->setChecked(s.dry_run_default);
        this->autoCompactCheck->setChecked(s.auto_compact_enabled);
        this->autoCompactThresholdSpin->setValue(s.auto_compact_threshold_tokens);

        this->aiCompletionCheck->setChecked(s.ai_completion_enabled);

        this->completionMinCharsSpin->setValue(s.completion_min_chars);

        this->completionDelayMsSpin->setValue(s.completion_delay_ms);

        if (this->completionModelCombo->lineEdit() != nullptr)
        {
            this->completionModelCombo->lineEdit()->setPlaceholderText(
                tr_t::tr("(Same as agent model)"));
        }
        this->completionModelCombo->addItems({
            // Fast/small models suitable for completion
            QStringLiteral("gpt-5.4-mini"),
            QStringLiteral("gpt-5.3-codex"),
            QStringLiteral("gpt-4.1-nano"),
            QStringLiteral("gpt-4.1-mini"),
            QStringLiteral("gpt-5-mini"),
            QStringLiteral("gpt-5.1-codex-mini"),
            QStringLiteral("gpt-5.1-codex"),
            QStringLiteral("gpt-5.2-codex"),
            QStringLiteral("claude-haiku-4.5"),
            QStringLiteral("claude-sonnet-4.6"),
            QStringLiteral("gemini-3-flash"),
            QStringLiteral("gemini-2.5-flash-lite"),
            QStringLiteral("gemini-2.5-flash"),
            QStringLiteral("deepseek-coder"),
            QStringLiteral("qwen2.5-coder-32b"),
            QStringLiteral("codellama"),
        });
        this->completionModelCombo->setCurrentText(s.completion_model.trimmed());

        populateEffortCombo(this->completionThinkingCombo);
        selectEffortValue(this->completionThinkingCombo, s.completion_thinking_level, 0);

        populateEffortCombo(this->completionReasoningCombo);
        selectEffortValue(this->completionReasoningCombo, s.completion_reasoning_effort, 0);

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
        this->qdrantTimeoutSpin->setValue(s.qdrant_timeout_sec);
        this->vectorSearchMaxIndexingThreadsSpin->setValue(s.vector_search_max_indexing_threads);

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
        installCheckSettingsDirtyTrigger(this->maxTokensSpin);
        installCheckSettingsDirtyTrigger(this->copilotModelCombo);
        installCheckSettingsDirtyTrigger(this->copilotNodeEdit);
        installCheckSettingsDirtyTrigger(this->copilotSidecarEdit);
        installCheckSettingsDirtyTrigger(this->copilotCompletionTimeoutSpin);
        installCheckSettingsDirtyTrigger(this->localUrlEdit);
        installCheckSettingsDirtyTrigger(this->localEndpointEdit);
        installCheckSettingsDirtyTrigger(this->localSimpleCheck);
        installCheckSettingsDirtyTrigger(this->localHeadersEdit);
        installCheckSettingsDirtyTrigger(this->ollamaUrlEdit);
        installCheckSettingsDirtyTrigger(this->ollamaModelCombo);
        installCheckSettingsDirtyTrigger(this->maxIterSpin);
        installCheckSettingsDirtyTrigger(this->maxToolsSpin);
        installCheckSettingsDirtyTrigger(this->maxDiffSpin);
        installCheckSettingsDirtyTrigger(this->maxFilesSpin);
        installCheckSettingsDirtyTrigger(this->dryRunCheck);
        installCheckSettingsDirtyTrigger(this->autoCompactCheck);
        installCheckSettingsDirtyTrigger(this->autoCompactThresholdSpin);
        connect(this->autoCompactCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        installCheckSettingsDirtyTrigger(this->aiCompletionCheck);
        installCheckSettingsDirtyTrigger(this->completionMinCharsSpin);
        installCheckSettingsDirtyTrigger(this->completionDelayMsSpin);
        installCheckSettingsDirtyTrigger(this->completionModelCombo);
        installCheckSettingsDirtyTrigger(this->completionThinkingCombo);
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
        installCheckSettingsDirtyTrigger(this->qdrantTimeoutSpin);
        installCheckSettingsDirtyTrigger(this->vectorSearchMaxIndexingThreadsSpin);
        installCheckSettingsDirtyTrigger(this->debugLoggingCheck);
        installCheckSettingsDirtyTrigger(this->detailedRequestLoggingCheck);
        installCheckSettingsDirtyTrigger(this->inlineDiffRefinementCheck);
        installCheckSettingsDirtyTrigger(this->agentDebugCheck);
        // QDoubleSpinBox not supported by installCheckSettingsDirtyTrigger
        connect(this->tempSpin, &QDoubleSpinBox::valueChanged, Utils::checkSettingsDirty);
        connect(this->completionModelCombo, &QComboBox::currentTextChanged,
                Utils::checkSettingsDirty);
        connect(this->detailedRequestLoggingCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        connect(this->systemPromptEdit, &QPlainTextEdit::textChanged, Utils::checkSettingsDirty);
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
        s.completion_min_chars = this->completionMinCharsSpin->value();
        s.completion_delay_ms = this->completionDelayMsSpin->value();
        s.completion_model = this->completionModelCombo->currentText().trimmed();
        s.completion_thinking_level = this->completionThinkingCombo->currentData().toString();
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
        s.inline_diff_refinement_enabled = this->inlineDiffRefinementCheck->isChecked();
        s.agent_debug = this->agentDebugCheck->isChecked();
        s.mcp_servers = this->mcpServersWidget->servers();
        return s;
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
            this->completionMinCharsSpin->value(),
            this->completionDelayMsSpin->value(),
            this->completionModelCombo->currentText().trimmed(),
            this->completionThinkingCombo->currentData().toString(),
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
        this->tempSpin->setValue(snap.temperature);
        this->maxTokensSpin->setValue(snap.max_tokens);

        this->copilotModelCombo->setCurrentText(snap.copilot_model);
        this->copilotNodeEdit->setText(snap.copilot_node_path);
        this->copilotSidecarEdit->setText(snap.copilot_sidecar_path);
        this->copilotCompletionTimeoutSpin->setValue(snap.copilot_completion_timeout_sec);

        this->localUrlEdit->setText(snap.local_base_url);
        this->localEndpointEdit->setText(snap.local_endpoint_path);
        this->localSimpleCheck->setChecked(snap.local_simple_mode);
        this->localHeadersEdit->setText(snap.local_custom_headers);

        this->ollamaUrlEdit->setText(snap.ollama_base_url);
        this->ollamaModelCombo->setCurrentText(snap.ollama_model);

        this->maxIterSpin->setValue(snap.max_iterations);
        this->maxToolsSpin->setValue(snap.max_tool_calls);
        this->maxDiffSpin->setValue(snap.max_diff_lines);
        this->maxFilesSpin->setValue(snap.max_changed_files);

        this->dryRunCheck->setChecked(snap.dry_run_default);
        this->autoCompactCheck->setChecked(snap.auto_compact_enabled);
        this->autoCompactThresholdSpin->setValue(snap.auto_compact_threshold_tokens);
        this->aiCompletionCheck->setChecked(snap.ai_completion_enabled);
        this->completionMinCharsSpin->setValue(snap.completion_min_chars);
        this->completionDelayMsSpin->setValue(snap.completion_delay_ms);

        this->completionModelCombo->setCurrentText(snap.completion_model.trimmed());

        selectEffortValue(this->completionThinkingCombo, snap.completion_thinking_level, 0);
        selectEffortValue(this->completionReasoningCombo, snap.completion_reasoning_effort, 0);

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
        this->qdrantTimeoutSpin->setValue(snap.qdrant_timeout_sec);
        this->vectorSearchMaxIndexingThreadsSpin->setValue(
            snap.vector_search_max_indexing_threads);
        this->refreshVectorSearchUiState();

        this->debugLoggingCheck->setChecked(snap.debug_logging);
        this->detailedRequestLoggingCheck->setChecked(snap.detailed_request_logging);
        this->systemPromptEdit->setPlainText(snap.system_prompt);
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
    QDoubleSpinBox *tempSpin;
    QSpinBox *maxTokensSpin;
    QLineEdit *copilotNodeEdit, *copilotSidecarEdit;
    QComboBox *copilotModelCombo;
    QSpinBox *copilotCompletionTimeoutSpin;
    QLineEdit *localUrlEdit, *localEndpointEdit, *localHeadersEdit;
    QCheckBox *localSimpleCheck;
    QLineEdit *ollamaUrlEdit;
    QComboBox *ollamaModelCombo;
    QSpinBox *maxIterSpin, *maxToolsSpin, *maxDiffSpin, *maxFilesSpin;
    QCheckBox *dryRunCheck;
    QCheckBox *autoCompactCheck;
    QSpinBox *autoCompactThresholdSpin;
    QCheckBox *aiCompletionCheck;
    QSpinBox *completionMinCharsSpin;
    QSpinBox *completionDelayMsSpin;
    QComboBox *completionModelCombo;
    QComboBox *completionThinkingCombo;
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
    QSpinBox *qdrantTimeoutSpin;
    QSpinBox *vectorSearchMaxIndexingThreadsSpin;
    QPushButton *qdrantTestConnectionButton;
    QCheckBox *debugLoggingCheck;
    QCheckBox *detailedRequestLoggingCheck;
    QPlainTextEdit *systemPromptEdit;
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
