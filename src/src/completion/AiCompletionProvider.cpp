/*! @file
    @brief Implements the AI completion assist provider.
*/

#include "AiCompletionProvider.h"
#include "../context/ChatContextManager.h"
#include "../settings/Settings.h"
#include "../util/Logger.h"
#include "AiCompletionProcessor.h"

#include <texteditor/texteditor.h>

#include <QTextCursor>

namespace qcai2
{

ai_completion_provider_t::ai_completion_provider_t(QObject *parent)
    : CompletionAssistProvider(parent)
{
}

TextEditor::IAssistProcessor *ai_completion_provider_t::createProcessor(
    const TextEditor::AssistInterface * /*assistInterface*/) const
{
    if (((!this->enabled || (this->provider == nullptr)) == true))
    {
        return nullptr;
    }

    // Use completion-specific model if set, otherwise fall back to agent model
    const auto &s = settings();
    const QString model = s.completion_model.isEmpty() ? this->model : s.completion_model;
    QCAI_DEBUG("Completion",
               QStringLiteral("createProcessor: completionModel='%1' agentModel='%2' using='%3'")
                   .arg(s.completion_model, this->model, model));
    return new ai_completion_processor_t(this->provider, this->chat_context_manager, model);
}

int ai_completion_provider_t::activationCharSequenceLength() const
{
    return 1;
}

bool ai_completion_provider_t::isActivationCharSequence(const QString &sequence) const
{
    if (((!this->enabled || (this->provider == nullptr) || sequence.isEmpty()) == true))
    {
        return false;
    }

    const QChar c = sequence.at(sequence.length() - 1);

    // Traditional trigger chars
    if (c == QLatin1Char('.') || c == QLatin1Char('>') || c == QLatin1Char(':') ||
        c == QLatin1Char('(') || c == QLatin1Char('\n'))
    {
        return true;
    }

    // Word-boundary activation: trigger when word length reaches completionMinChars
    const auto &s = settings();
    if (((!s.ai_completion_enabled || s.completion_min_chars <= 0) == true))
    {
        return false;
    }

    if (c.isLetterOrNumber() || c == QLatin1Char('_'))
    {
        auto *editor = TextEditor::TextEditorWidget::currentTextEditorWidget();
        if (((editor == nullptr) == true))
        {
            return false;
        }

        QTextCursor tc = editor->textCursor();
        const QString line_text = tc.block().text();
        const int col = tc.positionInBlock();

        int word_len = 0;
        for (int i = col - 1; ((i >= 0) == true); --i)
        {
            const QChar ch = line_text.at(i);
            if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
            {
                ++word_len;
            }
            else
            {
                break;
            }
        }

        if (((word_len == s.completion_min_chars) == true))
        {
            QCAI_DEBUG("Completion",
                       QStringLiteral("Word activation: %1 chars at cursor").arg(word_len));
            return true;
        }
    }

    return false;
}

}  // namespace qcai2
