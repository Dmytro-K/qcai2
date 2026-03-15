/*! @file
    @brief Implements the Qt Creator settings page for the plugin.
*/

#include "SettingsPage.h"
#include "../../qcai2tr.h"
#include "../util/Logger.h"
#include "McpServersWidget.h"
#include "Settings.h"
#include "ui_SettingsWidget.h"

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
    combo->addItem(Tr::tr("Off"), QStringLiteral("off"));
    combo->addItem(Tr::tr("Low"), QStringLiteral("low"));
    combo->addItem(Tr::tr("Medium"), QStringLiteral("medium"));
    combo->addItem(Tr::tr("High"), QStringLiteral("high"));
}

void selectEffortValue(QComboBox *combo, const QString &value, int fallbackIndex)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : fallbackIndex);
}

}  // namespace

class SettingsWidget : public Core::IOptionsPageWidget
{
    struct Snapshot
    {
        QString provider;
        QString baseUrl;
        QString apiKey;
        QString modelName;
        QString reasoningEffort;
        QString thinkingLevel;
        double temperature = 0;
        int maxTokens = 0;
        QString copilotModel;
        QString copilotNodePath;
        QString copilotSidecarPath;
        QString localBaseUrl;
        QString localEndpointPath;
        bool localSimpleMode = false;
        QString localCustomHeaders;
        QString ollamaBaseUrl;
        QString ollamaModel;
        int maxIterations = 0;
        int maxToolCalls = 0;
        int maxDiffLines = 0;
        int maxChangedFiles = 0;
        bool dryRunDefault = false;
        bool aiCompletionEnabled = false;
        int completionMinChars = 0;
        int completionDelayMs = 0;
        QString completionModel;
        QString completionThinkingLevel;
        QString completionReasoningEffort;
        bool debugLogging = false;
        bool agentDebug = false;
        qtmcp::ServerDefinitions mcpServers;

        friend bool operator==(const Snapshot &, const Snapshot &) = default;
    };

public:
    SettingsWidget()
    {
        m_ui = std::make_unique<Ui::SettingsWidget>();
        m_ui->setupUi(this);

        auto &s = settings();

        m_providerCombo = m_ui->providerCombo;
        m_baseUrlCombo = m_ui->baseUrlCombo;
        m_apiKeyEdit = m_ui->apiKeyEdit;
        m_modelCombo = m_ui->modelCombo;
        m_reasoningCombo = m_ui->reasoningCombo;
        m_thinkingCombo = m_ui->thinkingCombo;
        m_tempSpin = m_ui->tempSpin;
        m_maxTokensSpin = m_ui->maxTokensSpin;
        m_copilotModelCombo = m_ui->copilotModelCombo;
        m_copilotNodeEdit = m_ui->copilotNodeEdit;
        m_copilotSidecarEdit = m_ui->copilotSidecarEdit;
        m_localUrlEdit = m_ui->localUrlEdit;
        m_localEndpointEdit = m_ui->localEndpointEdit;
        m_localSimpleCheck = m_ui->localSimpleCheck;
        m_localHeadersEdit = m_ui->localHeadersEdit;
        m_ollamaUrlEdit = m_ui->ollamaUrlEdit;
        m_ollamaModelCombo = m_ui->ollamaModelCombo;
        m_maxIterSpin = m_ui->maxIterSpin;
        m_maxToolsSpin = m_ui->maxToolsSpin;
        m_maxDiffSpin = m_ui->maxDiffSpin;
        m_maxFilesSpin = m_ui->maxFilesSpin;
        m_dryRunCheck = m_ui->dryRunCheck;
        m_aiCompletionCheck = m_ui->aiCompletionCheck;
        m_completionMinCharsSpin = m_ui->completionMinCharsSpin;
        m_completionDelayMsSpin = m_ui->completionDelayMsSpin;
        m_completionModelCombo = m_ui->completionModelCombo;
        m_completionThinkingCombo = m_ui->completionThinkingCombo;
        m_completionReasoningCombo = m_ui->completionReasoningCombo;
        m_debugLoggingCheck = m_ui->debugLoggingCheck;
        m_agentDebugCheck = m_ui->agentDebugCheck;
        m_mcpServersWidget = new McpServersWidget(this);
        m_mcpServersWidget->setServers(s.mcpServers);
        m_ui->settingsTabs->insertTab(1, m_mcpServersWidget, Tr::tr("MCP Servers"));

        // Provider selection
        m_providerCombo->addItem(Tr::tr("OpenAI-Compatible"), QStringLiteral("openai"));
        m_providerCombo->addItem(Tr::tr("GitHub Copilot"), QStringLiteral("copilot"));
        m_providerCombo->addItem(Tr::tr("Local HTTP"), QStringLiteral("local"));
        m_providerCombo->addItem(Tr::tr("Ollama (Local)"), QStringLiteral("ollama"));
        int idx = m_providerCombo->findData(s.provider);
        if (((idx >= 0) == true))
        {
            m_providerCombo->setCurrentIndex(idx);
        }

        // OpenAI settings
        // Base URL combo: editable, with popular providers
        m_baseUrlCombo->addItems({
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
        m_baseUrlCombo->setCurrentText(s.baseUrl);
        m_apiKeyEdit->setText(s.apiKey);

        // Model combo: editable, with presets
        m_modelCombo->addItems({
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
        m_modelCombo->setCurrentText(s.modelName);

        populateEffortCombo(m_reasoningCombo);
        selectEffortValue(m_reasoningCombo, s.reasoningEffort, 2);

        populateEffortCombo(m_thinkingCombo);
        selectEffortValue(m_thinkingCombo, s.thinkingLevel, 2);

        m_tempSpin->setValue(s.temperature);

        m_maxTokensSpin->setValue(s.maxTokens);

        // GitHub Copilot (sidecar)
        repopulateEditableCombo(m_copilotModelCombo, modelCatalog().copilotModels(),
                                s.copilotModel);
        connect(&modelCatalog(), &ModelCatalog::copilotModelsChanged, this,
                [this](const QStringList &models) {
                    repopulateEditableCombo(m_copilotModelCombo, models,
                                            m_copilotModelCombo->currentText());
                });

        m_copilotNodeEdit->setText(s.copilotNodePath);
        m_copilotSidecarEdit->setText(s.copilotSidecarPath);

        // Local provider settings
        m_localUrlEdit->setText(s.localBaseUrl);
        m_localEndpointEdit->setText(s.localEndpointPath);
        m_localSimpleCheck->setChecked(s.localSimpleMode);
        m_localHeadersEdit->setText(s.localCustomHeaders);

        // Ollama
        m_ollamaUrlEdit->setText(s.ollamaBaseUrl);
        m_ollamaModelCombo->addItems({
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
        m_ollamaModelCombo->setCurrentText(s.ollamaModel);

        // Safety
        m_maxIterSpin->setValue(s.maxIterations);
        m_maxToolsSpin->setValue(s.maxToolCalls);
        m_maxDiffSpin->setValue(s.maxDiffLines);
        m_maxFilesSpin->setValue(s.maxChangedFiles);

        m_dryRunCheck->setChecked(s.dryRunDefault);

        m_aiCompletionCheck->setChecked(s.aiCompletionEnabled);

        m_completionMinCharsSpin->setValue(s.completionMinChars);

        m_completionDelayMsSpin->setValue(s.completionDelayMs);

        m_completionModelCombo->addItem(Tr::tr("(Same as agent model)"), QStringLiteral(""));
        m_completionModelCombo->addItems({
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
        if (s.completionModel.isEmpty())
        {
            m_completionModelCombo->setCurrentIndex(0);
        }
        else
        {
            m_completionModelCombo->setCurrentText(s.completionModel);
        }

        populateEffortCombo(m_completionThinkingCombo);
        selectEffortValue(m_completionThinkingCombo, s.completionThinkingLevel, 0);

        populateEffortCombo(m_completionReasoningCombo);
        selectEffortValue(m_completionReasoningCombo, s.completionReasoningEffort, 0);

        m_debugLoggingCheck->setChecked(s.debugLogging);

        m_agentDebugCheck->setChecked(s.agentDebug);

        m_snapshot = captureSnapshot();

        // Notify the framework when any widget value changes so it can poll isDirty().
        using Utils::installCheckSettingsDirtyTrigger;
        installCheckSettingsDirtyTrigger(m_providerCombo);
        installCheckSettingsDirtyTrigger(m_baseUrlCombo);
        installCheckSettingsDirtyTrigger(m_apiKeyEdit);
        installCheckSettingsDirtyTrigger(m_modelCombo);
        installCheckSettingsDirtyTrigger(m_reasoningCombo);
        installCheckSettingsDirtyTrigger(m_thinkingCombo);
        installCheckSettingsDirtyTrigger(m_maxTokensSpin);
        installCheckSettingsDirtyTrigger(m_copilotModelCombo);
        installCheckSettingsDirtyTrigger(m_copilotNodeEdit);
        installCheckSettingsDirtyTrigger(m_copilotSidecarEdit);
        installCheckSettingsDirtyTrigger(m_localUrlEdit);
        installCheckSettingsDirtyTrigger(m_localEndpointEdit);
        installCheckSettingsDirtyTrigger(m_localSimpleCheck);
        installCheckSettingsDirtyTrigger(m_localHeadersEdit);
        installCheckSettingsDirtyTrigger(m_ollamaUrlEdit);
        installCheckSettingsDirtyTrigger(m_ollamaModelCombo);
        installCheckSettingsDirtyTrigger(m_maxIterSpin);
        installCheckSettingsDirtyTrigger(m_maxToolsSpin);
        installCheckSettingsDirtyTrigger(m_maxDiffSpin);
        installCheckSettingsDirtyTrigger(m_maxFilesSpin);
        installCheckSettingsDirtyTrigger(m_dryRunCheck);
        installCheckSettingsDirtyTrigger(m_aiCompletionCheck);
        installCheckSettingsDirtyTrigger(m_completionMinCharsSpin);
        installCheckSettingsDirtyTrigger(m_completionDelayMsSpin);
        installCheckSettingsDirtyTrigger(m_completionModelCombo);
        installCheckSettingsDirtyTrigger(m_completionThinkingCombo);
        installCheckSettingsDirtyTrigger(m_completionReasoningCombo);
        installCheckSettingsDirtyTrigger(m_debugLoggingCheck);
        installCheckSettingsDirtyTrigger(m_agentDebugCheck);
        // QDoubleSpinBox not supported by installCheckSettingsDirtyTrigger
        connect(m_tempSpin, &QDoubleSpinBox::valueChanged, Utils::checkSettingsDirty);
        connect(m_mcpServersWidget, &McpServersWidget::changed, Utils::checkSettingsDirty);
    }

    bool isDirty() const override
    {
        return captureSnapshot() != m_snapshot;
    }

    void apply() override
    {
        auto &s = settings();
        s.provider = m_providerCombo->currentData().toString();
        s.baseUrl = m_baseUrlCombo->currentText();
        s.apiKey = m_apiKeyEdit->text();
        s.modelName = m_modelCombo->currentText();
        s.reasoningEffort = m_reasoningCombo->currentData().toString();
        s.thinkingLevel = m_thinkingCombo->currentData().toString();
        s.temperature = m_tempSpin->value();
        s.maxTokens = m_maxTokensSpin->value();

        s.copilotModel = m_copilotModelCombo->currentText();
        s.copilotNodePath = m_copilotNodeEdit->text();
        s.copilotSidecarPath = m_copilotSidecarEdit->text();

        s.localBaseUrl = m_localUrlEdit->text();
        s.localEndpointPath = m_localEndpointEdit->text();
        s.localSimpleMode = m_localSimpleCheck->isChecked();
        s.localCustomHeaders = m_localHeadersEdit->text();

        s.ollamaBaseUrl = m_ollamaUrlEdit->text();
        s.ollamaModel = m_ollamaModelCombo->currentText();

        s.maxIterations = m_maxIterSpin->value();
        s.maxToolCalls = m_maxToolsSpin->value();
        s.maxDiffLines = m_maxDiffSpin->value();
        s.maxChangedFiles = m_maxFilesSpin->value();

        s.dryRunDefault = m_dryRunCheck->isChecked();
        s.aiCompletionEnabled = m_aiCompletionCheck->isChecked();
        s.completionMinChars = m_completionMinCharsSpin->value();
        s.completionDelayMs = m_completionDelayMsSpin->value();
        const int cIdx = m_completionModelCombo->currentIndex();
        s.completionModel = (cIdx == 0) ? QString() : m_completionModelCombo->currentText();
        s.completionThinkingLevel = m_completionThinkingCombo->currentData().toString();
        s.completionReasoningEffort = m_completionReasoningCombo->currentData().toString();
        s.debugLogging = m_debugLoggingCheck->isChecked();
        s.agentDebug = m_agentDebugCheck->isChecked();
        s.mcpServers = m_mcpServersWidget->servers();
        s.save();

        Logger::instance().setEnabled(s.debugLogging);

        m_snapshot = captureSnapshot();
    }

    void cancel() override
    {
        restoreSnapshot(m_snapshot);
    }

private:
    Snapshot captureSnapshot() const
    {
        const int cIdx = m_completionModelCombo->currentIndex();
        return {
            m_providerCombo->currentData().toString(),
            m_baseUrlCombo->currentText(),
            m_apiKeyEdit->text(),
            m_modelCombo->currentText(),
            m_reasoningCombo->currentData().toString(),
            m_thinkingCombo->currentData().toString(),
            m_tempSpin->value(),
            m_maxTokensSpin->value(),
            m_copilotModelCombo->currentText(),
            m_copilotNodeEdit->text(),
            m_copilotSidecarEdit->text(),
            m_localUrlEdit->text(),
            m_localEndpointEdit->text(),
            m_localSimpleCheck->isChecked(),
            m_localHeadersEdit->text(),
            m_ollamaUrlEdit->text(),
            m_ollamaModelCombo->currentText(),
            m_maxIterSpin->value(),
            m_maxToolsSpin->value(),
            m_maxDiffSpin->value(),
            m_maxFilesSpin->value(),
            m_dryRunCheck->isChecked(),
            m_aiCompletionCheck->isChecked(),
            m_completionMinCharsSpin->value(),
            m_completionDelayMsSpin->value(),
            (cIdx == 0) ? QString() : m_completionModelCombo->currentText(),
            m_completionThinkingCombo->currentData().toString(),
            m_completionReasoningCombo->currentData().toString(),
            m_debugLoggingCheck->isChecked(),
            m_agentDebugCheck->isChecked(),
            m_mcpServersWidget->servers(),
        };
    }

    void restoreSnapshot(const Snapshot &snap)
    {
        const int provIdx = m_providerCombo->findData(snap.provider);
        if (provIdx >= 0)
            m_providerCombo->setCurrentIndex(provIdx);

        m_baseUrlCombo->setCurrentText(snap.baseUrl);
        m_apiKeyEdit->setText(snap.apiKey);
        m_modelCombo->setCurrentText(snap.modelName);
        selectEffortValue(m_reasoningCombo, snap.reasoningEffort, 2);
        selectEffortValue(m_thinkingCombo, snap.thinkingLevel, 2);
        m_tempSpin->setValue(snap.temperature);
        m_maxTokensSpin->setValue(snap.maxTokens);

        m_copilotModelCombo->setCurrentText(snap.copilotModel);
        m_copilotNodeEdit->setText(snap.copilotNodePath);
        m_copilotSidecarEdit->setText(snap.copilotSidecarPath);

        m_localUrlEdit->setText(snap.localBaseUrl);
        m_localEndpointEdit->setText(snap.localEndpointPath);
        m_localSimpleCheck->setChecked(snap.localSimpleMode);
        m_localHeadersEdit->setText(snap.localCustomHeaders);

        m_ollamaUrlEdit->setText(snap.ollamaBaseUrl);
        m_ollamaModelCombo->setCurrentText(snap.ollamaModel);

        m_maxIterSpin->setValue(snap.maxIterations);
        m_maxToolsSpin->setValue(snap.maxToolCalls);
        m_maxDiffSpin->setValue(snap.maxDiffLines);
        m_maxFilesSpin->setValue(snap.maxChangedFiles);

        m_dryRunCheck->setChecked(snap.dryRunDefault);
        m_aiCompletionCheck->setChecked(snap.aiCompletionEnabled);
        m_completionMinCharsSpin->setValue(snap.completionMinChars);
        m_completionDelayMsSpin->setValue(snap.completionDelayMs);

        if (snap.completionModel.isEmpty())
            m_completionModelCombo->setCurrentIndex(0);
        else
            m_completionModelCombo->setCurrentText(snap.completionModel);

        selectEffortValue(m_completionThinkingCombo, snap.completionThinkingLevel, 0);
        selectEffortValue(m_completionReasoningCombo, snap.completionReasoningEffort, 0);

        m_debugLoggingCheck->setChecked(snap.debugLogging);
        m_agentDebugCheck->setChecked(snap.agentDebug);
        m_mcpServersWidget->setServers(snap.mcpServers);
    }

    Snapshot m_snapshot;
    std::unique_ptr<Ui::SettingsWidget> m_ui;
    QComboBox *m_providerCombo;
    QComboBox *m_baseUrlCombo;
    QLineEdit *m_apiKeyEdit;
    QComboBox *m_modelCombo;
    QComboBox *m_reasoningCombo;
    QComboBox *m_thinkingCombo;
    QDoubleSpinBox *m_tempSpin;
    QSpinBox *m_maxTokensSpin;
    QLineEdit *m_copilotNodeEdit, *m_copilotSidecarEdit;
    QComboBox *m_copilotModelCombo;
    QLineEdit *m_localUrlEdit, *m_localEndpointEdit, *m_localHeadersEdit;
    QCheckBox *m_localSimpleCheck;
    QLineEdit *m_ollamaUrlEdit;
    QComboBox *m_ollamaModelCombo;
    QSpinBox *m_maxIterSpin, *m_maxToolsSpin, *m_maxDiffSpin, *m_maxFilesSpin;
    QCheckBox *m_dryRunCheck;
    QCheckBox *m_aiCompletionCheck;
    QSpinBox *m_completionMinCharsSpin;
    QSpinBox *m_completionDelayMsSpin;
    QComboBox *m_completionModelCombo;
    QComboBox *m_completionThinkingCombo;
    QComboBox *m_completionReasoningCombo;
    QCheckBox *m_debugLoggingCheck;
    QCheckBox *m_agentDebugCheck;
    McpServersWidget *m_mcpServersWidget = nullptr;
};

SettingsPage::SettingsPage()
{
    setId("qcai2.Settings");
    setDisplayName(Tr::tr("Qcai2"));
    setCategory("qcai2");
    IOptionsPage::registerCategory(
        "qcai2", Tr::tr("Qcai2"),
        Utils::FilePath::fromString(QStringLiteral(":/qcai2/ai-agent-icon.svg")));

    setWidgetCreator([]() -> Core::IOptionsPageWidget * { return new SettingsWidget; });
}

}  // namespace qcai2
