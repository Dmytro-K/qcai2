#pragma once

#include <texteditor/codeassist/iassistprocessor.h>

#include <QString>
#include <memory>

namespace Qcai2 {

class IAIProvider;

// Async processor that sends editor context to AI and returns completions.
class AiCompletionProcessor : public TextEditor::IAssistProcessor
{
public:
    AiCompletionProcessor(IAIProvider *provider, const QString &model);
    ~AiCompletionProcessor() override;

    bool running() override;
    void cancel() override;

protected:
    TextEditor::IAssistProposal *perform() override;

private:
    IAIProvider *m_provider;
    QString m_model;
    bool m_running = false;
    bool m_cancelled = false;
    // Guard against use-after-free in async callback
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace Qcai2
