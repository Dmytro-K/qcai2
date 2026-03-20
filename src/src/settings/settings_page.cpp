/*! @file
    @brief Implements the Qt Creator settings page for the plugin.
*/

#include "settings_page.h"
#include "../../qcai2tr.h"
#include "../util/logger.h"
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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

}  // namespace

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
        bool debug_logging = false;
        bool detailed_request_logging = false;
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
        this->debugLoggingCheck = this->ui->debugLoggingCheck;
        this->detailedRequestLoggingCheck = this->ui->detailedRequestLoggingCheck;
        this->agentDebugCheck = this->ui->agentDebugCheck;
        this->mcpServersWidget = new mcp_servers_widget_t(this);
        this->mcpServersWidget->set_servers(s.mcp_servers);
        this->ui->settingsTabs->insertTab(1, this->mcpServersWidget, tr_t::tr("MCP Servers"));

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
            QStringLiteral("gpt-5.2"),
            QStringLiteral("gpt-5.2-codex"),
            QStringLiteral("gpt-5.1"),
            QStringLiteral("gpt-5.1-codex"),
            QStringLiteral("gpt-5"),
            QStringLiteral("gpt-5-mini"),
            // OpenAI — GPT-4.1
            QStringLiteral("gpt-4.1"),
            QStringLiteral("gpt-4.1-mini"),
            QStringLiteral("gpt-4.1-nano"),
            // OpenAI — O-series reasoning
            QStringLiteral("o3"),
            QStringLiteral("o4-mini"),
            // OpenAI — GPT-4o (legacy)
            QStringLiteral("gpt-4o"),
            QStringLiteral("gpt-4o-mini"),
            // Anthropic Claude
            QStringLiteral("claude-opus-4-6"),
            QStringLiteral("claude-sonnet-4-6"),
            QStringLiteral("claude-sonnet-4-5"),
            QStringLiteral("claude-haiku-4-5"),
            // Google Gemini
            QStringLiteral("gemini-3-pro"),
            QStringLiteral("gemini-3-flash"),
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

        this->completionModelCombo->addItem(tr_t::tr("(Same as agent model)"), QStringLiteral(""));
        this->completionModelCombo->addItems({
            // Fast/small models suitable for completion
            QStringLiteral("gpt-4.1-nano"),
            QStringLiteral("gpt-4.1-mini"),
            QStringLiteral("gpt-5-mini"),
            QStringLiteral("gpt-5.1-codex-mini"),
            QStringLiteral("gpt-5.1-codex"),
            QStringLiteral("gpt-5.2-codex"),
            QStringLiteral("claude-haiku-4-5"),
            QStringLiteral("claude-sonnet-4-6"),
            QStringLiteral("gemini-3-flash"),
            QStringLiteral("gemini-2.5-flash-lite"),
            QStringLiteral("gemini-2.5-flash"),
            QStringLiteral("deepseek-coder"),
            QStringLiteral("qwen2.5-coder-32b"),
            QStringLiteral("codellama"),
        });
        if (s.completion_model.isEmpty())
        {
            this->completionModelCombo->setCurrentIndex(0);
        }
        else
        {
            this->completionModelCombo->setCurrentText(s.completion_model);
        }

        populateEffortCombo(this->completionThinkingCombo);
        selectEffortValue(this->completionThinkingCombo, s.completion_thinking_level, 0);

        populateEffortCombo(this->completionReasoningCombo);
        selectEffortValue(this->completionReasoningCombo, s.completion_reasoning_effort, 0);

        this->debugLoggingCheck->setChecked(s.debug_logging);
        this->detailedRequestLoggingCheck->setChecked(s.detailed_request_logging);

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
        installCheckSettingsDirtyTrigger(this->debugLoggingCheck);
        installCheckSettingsDirtyTrigger(this->detailedRequestLoggingCheck);
        installCheckSettingsDirtyTrigger(this->agentDebugCheck);
        // QDoubleSpinBox not supported by installCheckSettingsDirtyTrigger
        connect(this->tempSpin, &QDoubleSpinBox::valueChanged, Utils::checkSettingsDirty);
        connect(this->detailedRequestLoggingCheck, &QCheckBox::toggled, Utils::checkSettingsDirty);
        connect(this->mcpServersWidget, &mcp_servers_widget_t::changed, Utils::checkSettingsDirty);
    }

    bool isDirty() const override
    {
        return this->captureSnapshot() != this->snapshot;
    }

    void apply() override
    {
        auto &s = settings();
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
        const int cIdx = this->completionModelCombo->currentIndex();
        s.completion_model = (cIdx == 0) ? QString() : this->completionModelCombo->currentText();
        s.completion_thinking_level = this->completionThinkingCombo->currentData().toString();
        s.completion_reasoning_effort = this->completionReasoningCombo->currentData().toString();
        s.debug_logging = this->debugLoggingCheck->isChecked();
        s.detailed_request_logging = this->detailedRequestLoggingCheck->isChecked();
        s.agent_debug = this->agentDebugCheck->isChecked();
        s.mcp_servers = this->mcpServersWidget->servers();
        s.save();

        logger_t::instance().set_enabled(s.debug_logging);

        this->snapshot = this->captureSnapshot();
    }

    void cancel() override
    {
        this->restoreSnapshot(this->snapshot);
    }

private:
    snapshot_t captureSnapshot() const
    {
        const int cIdx = this->completionModelCombo->currentIndex();
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
            (cIdx == 0) ? QString() : this->completionModelCombo->currentText(),
            this->completionThinkingCombo->currentData().toString(),
            this->completionReasoningCombo->currentData().toString(),
            this->debugLoggingCheck->isChecked(),
            this->detailedRequestLoggingCheck->isChecked(),
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

        if (snap.completion_model.isEmpty())
            this->completionModelCombo->setCurrentIndex(0);
        else
            this->completionModelCombo->setCurrentText(snap.completion_model);

        selectEffortValue(this->completionThinkingCombo, snap.completion_thinking_level, 0);
        selectEffortValue(this->completionReasoningCombo, snap.completion_reasoning_effort, 0);

        this->debugLoggingCheck->setChecked(snap.debug_logging);
        this->detailedRequestLoggingCheck->setChecked(snap.detailed_request_logging);
        this->agentDebugCheck->setChecked(snap.agent_debug);
        this->mcpServersWidget->set_servers(snap.mcp_servers);
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
    QCheckBox *debugLoggingCheck;
    QCheckBox *detailedRequestLoggingCheck;
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
