/*! @file
    @brief Implements the Qt Creator settings page for the plugin.
*/

#include "SettingsPage.h"
#include "../../qcai2tr.h"
#include "../util/Logger.h"
#include "Settings.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <utils/filepath.h>

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
            continue;
        uniqueItems.append(trimmed);
    }

    const QString selected = selectedText.trimmed();
    if (!selected.isEmpty() && !uniqueItems.contains(selected))
        uniqueItems.append(selected);

    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItems(uniqueItems);
    combo->setCurrentText(selected);
}

}  // namespace

class SettingsWidget : public Core::IOptionsPageWidget
{
public:
    SettingsWidget()
    {
        auto &s = settings();

        // Provider selection
        m_providerCombo = new QComboBox;
        m_providerCombo->addItem(Tr::tr("OpenAI-Compatible"), QStringLiteral("openai"));
        m_providerCombo->addItem(Tr::tr("GitHub Copilot"), QStringLiteral("copilot"));
        m_providerCombo->addItem(Tr::tr("Local HTTP"), QStringLiteral("local"));
        m_providerCombo->addItem(Tr::tr("Ollama (Local)"), QStringLiteral("ollama"));
        int idx = m_providerCombo->findData(s.provider);
        if (idx >= 0)
            m_providerCombo->setCurrentIndex(idx);

        // OpenAI settings
        // Base URL combo: editable, with popular providers
        m_baseUrlCombo = new QComboBox;
        m_baseUrlCombo->setEditable(true);
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
        m_apiKeyEdit = new QLineEdit(s.apiKey);
        m_apiKeyEdit->setEchoMode(QLineEdit::Password);

        // Model combo: editable, with presets
        m_modelCombo = new QComboBox;
        m_modelCombo->setEditable(true);
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

        m_thinkingCombo = new QComboBox;
        m_thinkingCombo->addItem(Tr::tr("Off"), QStringLiteral("off"));
        m_thinkingCombo->addItem(Tr::tr("Low"), QStringLiteral("low"));
        m_thinkingCombo->addItem(Tr::tr("Medium"), QStringLiteral("medium"));
        m_thinkingCombo->addItem(Tr::tr("High"), QStringLiteral("high"));
        const int thinkingIdx = m_thinkingCombo->findData(s.thinkingLevel);
        m_thinkingCombo->setCurrentIndex(thinkingIdx >= 0 ? thinkingIdx : 2);

        m_tempSpin = new QDoubleSpinBox;
        m_tempSpin->setRange(0.0, 2.0);
        m_tempSpin->setSingleStep(0.1);
        m_tempSpin->setValue(s.temperature);

        m_maxTokensSpin = new QSpinBox;
        m_maxTokensSpin->setRange(256, 128000);
        m_maxTokensSpin->setValue(s.maxTokens);

        // GitHub Copilot (sidecar)
        m_copilotModelCombo = new QComboBox;
        m_copilotModelCombo->setEditable(true);
        repopulateEditableCombo(m_copilotModelCombo, modelCatalog().copilotModels(),
                                s.copilotModel);
        connect(&modelCatalog(), &ModelCatalog::copilotModelsChanged, this,
                [this](const QStringList &models) {
                    repopulateEditableCombo(m_copilotModelCombo, models,
                                            m_copilotModelCombo->currentText());
                });

        m_copilotNodeEdit = new QLineEdit(s.copilotNodePath);
        m_copilotNodeEdit->setPlaceholderText(Tr::tr("node (from PATH)"));

        m_copilotSidecarEdit = new QLineEdit(s.copilotSidecarPath);
        m_copilotSidecarEdit->setPlaceholderText(Tr::tr("Auto-detect"));

        // Local provider settings
        m_localUrlEdit = new QLineEdit(s.localBaseUrl);
        m_localEndpointEdit = new QLineEdit(s.localEndpointPath);
        m_localSimpleCheck = new QCheckBox(Tr::tr("Simple {prompt} mode"));
        m_localSimpleCheck->setChecked(s.localSimpleMode);
        m_localHeadersEdit = new QLineEdit(s.localCustomHeaders);
        m_localHeadersEdit->setPlaceholderText(Tr::tr("Header: Value, Header2: Value2"));

        // Ollama
        m_ollamaUrlEdit = new QLineEdit(s.ollamaBaseUrl);
        m_ollamaModelCombo = new QComboBox;
        m_ollamaModelCombo->setEditable(true);
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
        m_maxIterSpin = new QSpinBox;
        m_maxIterSpin->setRange(1, 100);
        m_maxIterSpin->setValue(s.maxIterations);
        m_maxToolsSpin = new QSpinBox;
        m_maxToolsSpin->setRange(1, 1000);
        m_maxToolsSpin->setValue(s.maxToolCalls);
        m_maxDiffSpin = new QSpinBox;
        m_maxDiffSpin->setRange(10, 100000);
        m_maxDiffSpin->setValue(s.maxDiffLines);
        m_maxFilesSpin = new QSpinBox;
        m_maxFilesSpin->setRange(1, 1000);
        m_maxFilesSpin->setValue(s.maxChangedFiles);

        m_dryRunCheck = new QCheckBox(Tr::tr("Dry-run mode by default"));
        m_dryRunCheck->setChecked(s.dryRunDefault);

        m_aiCompletionCheck = new QCheckBox(Tr::tr("Enable AI code completion in editor"));
        m_aiCompletionCheck->setChecked(s.aiCompletionEnabled);

        m_completionMinCharsSpin = new QSpinBox;
        m_completionMinCharsSpin->setRange(1, 20);
        m_completionMinCharsSpin->setValue(s.completionMinChars);
        m_completionMinCharsSpin->setToolTip(
            Tr::tr("Trigger AI completion after typing this many characters"));

        m_completionDelayMsSpin = new QSpinBox;
        m_completionDelayMsSpin->setRange(100, 5000);
        m_completionDelayMsSpin->setSingleStep(100);
        m_completionDelayMsSpin->setSuffix(QStringLiteral(" ms"));
        m_completionDelayMsSpin->setValue(s.completionDelayMs);
        m_completionDelayMsSpin->setToolTip(Tr::tr("Delay before sending completion request"));

        m_completionModelCombo = new QComboBox;
        m_completionModelCombo->setEditable(true);
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
            m_completionModelCombo->setCurrentIndex(0);
        else
            m_completionModelCombo->setCurrentText(s.completionModel);
        m_completionModelCombo->setToolTip(
            Tr::tr("Leave empty to use the same model as the agent"));

        m_completionThinkingCombo = new QComboBox;
        m_completionThinkingCombo->addItem(Tr::tr("Off"), QStringLiteral("off"));
        m_completionThinkingCombo->addItem(Tr::tr("Low"), QStringLiteral("low"));
        m_completionThinkingCombo->addItem(Tr::tr("Medium"), QStringLiteral("medium"));
        m_completionThinkingCombo->addItem(Tr::tr("High"), QStringLiteral("high"));
        {
            const int ti = m_completionThinkingCombo->findData(s.completionThinkingLevel);
            m_completionThinkingCombo->setCurrentIndex(ti >= 0 ? ti : 0);
        }

        m_completionReasoningCombo = new QComboBox;
        m_completionReasoningCombo->addItem(Tr::tr("Off"), QStringLiteral("off"));
        m_completionReasoningCombo->addItem(Tr::tr("Low"), QStringLiteral("low"));
        m_completionReasoningCombo->addItem(Tr::tr("Medium"), QStringLiteral("medium"));
        m_completionReasoningCombo->addItem(Tr::tr("High"), QStringLiteral("high"));
        {
            const int ri = m_completionReasoningCombo->findData(s.completionReasoningEffort);
            m_completionReasoningCombo->setCurrentIndex(ri >= 0 ? ri : 0);
        }

        m_debugLoggingCheck = new QCheckBox(Tr::tr("Enable debug logging (Debug Log tab)"));
        m_debugLoggingCheck->setChecked(s.debugLogging);

        m_agentDebugCheck = new QCheckBox(Tr::tr("Agent Debug (show raw JSON in chat)"));
        m_agentDebugCheck->setChecked(s.agentDebug);

        // Layout helper
        auto addRow = [](QVBoxLayout *l, const QString &label, QWidget *w) {
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(label), 0);
            row->addWidget(w, 1);
            l->addLayout(row);

        };

        auto *tabs = new QTabWidget;

        // ── Tab 1: Providers ──
        auto *providersPage = new QWidget;
        auto *pl = new QVBoxLayout(providersPage);
        addRow(pl, Tr::tr("Active provider:"), m_providerCombo);

        auto *openaiGroup = new QGroupBox(Tr::tr("OpenAI-Compatible"));
        auto *oal = new QVBoxLayout(openaiGroup);
        addRow(oal, Tr::tr("Base URL:"), m_baseUrlCombo);
        addRow(oal, Tr::tr("API Key:"), m_apiKeyEdit);
        addRow(oal, Tr::tr("Model:"), m_modelCombo);
        addRow(oal, Tr::tr("Thinking:"), m_thinkingCombo);
        addRow(oal, Tr::tr("Temperature:"), m_tempSpin);
        addRow(oal, Tr::tr("Max Tokens:"), m_maxTokensSpin);
        pl->addWidget(openaiGroup);

        auto *copilotGroup = new QGroupBox(Tr::tr("GitHub Copilot (via copilot-sdk sidecar)"));
        auto *cl = new QVBoxLayout(copilotGroup);
        addRow(cl, Tr::tr("Model:"), m_copilotModelCombo);
        addRow(cl, Tr::tr("Node path:"), m_copilotNodeEdit);
        addRow(cl, Tr::tr("Sidecar path:"), m_copilotSidecarEdit);
        pl->addWidget(copilotGroup);

        auto *localGroup = new QGroupBox(Tr::tr("Local HTTP"));
        auto *ll = new QVBoxLayout(localGroup);
        addRow(ll, Tr::tr("Base URL:"), m_localUrlEdit);
        addRow(ll, Tr::tr("Endpoint:"), m_localEndpointEdit);
        ll->addWidget(m_localSimpleCheck);
        addRow(ll, Tr::tr("Headers:"), m_localHeadersEdit);
        pl->addWidget(localGroup);

        auto *ollamaGroup = new QGroupBox(Tr::tr("Ollama"));
        auto *ol = new QVBoxLayout(ollamaGroup);
        addRow(ol, Tr::tr("Base URL:"), m_ollamaUrlEdit);
        addRow(ol, Tr::tr("Model:"), m_ollamaModelCombo);
        pl->addWidget(ollamaGroup);

        pl->addStretch();

        auto *providersScroll = new QScrollArea;
        providersScroll->setWidget(providersPage);
        providersScroll->setWidgetResizable(true);
        providersScroll->setFrameShape(QFrame::NoFrame);
        tabs->addTab(providersScroll, Tr::tr("Providers"));

        // ── Tab 2: Code Completion ──
        auto *completionPage = new QWidget;
        auto *cml = new QVBoxLayout(completionPage);
        cml->addWidget(m_aiCompletionCheck);
        addRow(cml, Tr::tr("Completion model:"), m_completionModelCombo);
        addRow(cml, Tr::tr("Thinking:"), m_completionThinkingCombo);
        addRow(cml, Tr::tr("Reasoning effort:"), m_completionReasoningCombo);
        addRow(cml, Tr::tr("Min characters to trigger:"), m_completionMinCharsSpin);
        addRow(cml, Tr::tr("Trigger delay:"), m_completionDelayMsSpin);
        cml->addStretch();
        tabs->addTab(completionPage, Tr::tr("Code Completion"));

        // ── Tab 3: Safety & Behavior ──
        auto *safetyPage = new QWidget;
        auto *sl = new QVBoxLayout(safetyPage);

        auto *limitsGroup = new QGroupBox(Tr::tr("Agent Limits"));
        auto *liml = new QVBoxLayout(limitsGroup);
        addRow(liml, Tr::tr("Max iterations:"), m_maxIterSpin);
        addRow(liml, Tr::tr("Max tool calls:"), m_maxToolsSpin);
        addRow(liml, Tr::tr("Max diff lines:"), m_maxDiffSpin);
        addRow(liml, Tr::tr("Max changed files:"), m_maxFilesSpin);
        sl->addWidget(limitsGroup);

        auto *behaviorGroup = new QGroupBox(Tr::tr("Behavior"));
        auto *bl = new QVBoxLayout(behaviorGroup);
        bl->addWidget(m_dryRunCheck);
        bl->addWidget(m_debugLoggingCheck);
        bl->addWidget(m_agentDebugCheck);
        sl->addWidget(behaviorGroup);

        sl->addStretch();
        tabs->addTab(safetyPage, Tr::tr("Safety & Behavior"));

        auto *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(tabs);
    }

    void apply() override
    {
        auto &s = settings();
        s.provider = m_providerCombo->currentData().toString();
        s.baseUrl = m_baseUrlCombo->currentText();
        s.apiKey = m_apiKeyEdit->text();
        s.modelName = m_modelCombo->currentText();
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
        // If "(Same as agent model)" is selected, store empty string
        const int cIdx = m_completionModelCombo->currentIndex();
        s.completionModel = (cIdx == 0) ? QString() : m_completionModelCombo->currentText();
        s.completionThinkingLevel = m_completionThinkingCombo->currentData().toString();
        s.completionReasoningEffort = m_completionReasoningCombo->currentData().toString();
        s.debugLogging = m_debugLoggingCheck->isChecked();
        s.agentDebug = m_agentDebugCheck->isChecked();
        s.save();

        // Apply debug logging immediately
        Logger::instance().setEnabled(s.debugLogging);
    }

private:
    QComboBox *m_providerCombo;
    QComboBox *m_baseUrlCombo;
    QLineEdit *m_apiKeyEdit;
    QComboBox *m_modelCombo;
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

};

SettingsPage::SettingsPage()
{
    setId("qcai2.Settings");
    setDisplayName(Tr::tr("Qcai2"));
    setCategory("qcai2");
    IOptionsPage::registerCategory(
        "qcai2", Tr::tr("Qcai2"),
        Utils::FilePath::fromString(QStringLiteral(":/qcai2/ai-agent-icon.svg")));

    setWidgetCreator([]() -> QWidget * { return new SettingsWidget; });
}

}  // namespace qcai2
