#pragma once

#include <texteditor/codeassist/completionassistprovider.h>

namespace Qcai2 {

class IAIProvider;

// Provides AI-powered code completions in the editor.
class AiCompletionProvider : public TextEditor::CompletionAssistProvider
{
    Q_OBJECT
public:
    explicit AiCompletionProvider(QObject *parent = nullptr);

    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *assistInterface) const override;

    int activationCharSequenceLength() const override;
    bool isActivationCharSequence(const QString &sequence) const override;

    void setAiProvider(IAIProvider *provider) { m_provider = provider; }
    void setModel(const QString &model) { m_model = model; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    IAIProvider *m_provider = nullptr;
    QString m_model;
    bool m_enabled = true;
};

} // namespace Qcai2
