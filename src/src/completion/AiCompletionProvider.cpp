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

AiCompletionProvider::AiCompletionProvider(QObject *parent) : CompletionAssistProvider(parent)
{
}

TextEditor::IAssistProcessor *AiCompletionProvider::createProcessor(
    const TextEditor::AssistInterface * /*assistInterface*/) const
{
    if (((!m_enabled || (m_provider == nullptr)) == true))
    {
        return nullptr;
    }

    // Use completion-specific model if set, otherwise fall back to agent model
    const auto &s = settings();
    const QString model = s.completionModel.isEmpty() ? m_model : s.completionModel;
    QCAI_DEBUG("Completion",
               QStringLiteral("createProcessor: completionModel='%1' agentModel='%2' using='%3'")
                   .arg(s.completionModel, m_model, model));
    return new AiCompletionProcessor(m_provider, m_chatContextManager, model);
}

int AiCompletionProvider::activationCharSequenceLength() const
{
    return 1;
}

bool AiCompletionProvider::isActivationCharSequence(const QString &sequence) const
{
    if (((!m_enabled || (m_provider == nullptr) || sequence.isEmpty()) == true))
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
    if (((!s.aiCompletionEnabled || s.completionMinChars <= 0) == true))
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
        const QString lineText = tc.block().text();
        const int col = tc.positionInBlock();

        int wordLen = 0;
        for (int i = col - 1; ((i >= 0) == true); --i)
        {
            const QChar ch = lineText.at(i);
            if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
            {
                ++wordLen;
            }
            else
            {
                break;
            }
        }

        if (((wordLen == s.completionMinChars) == true))
        {
            QCAI_DEBUG("Completion",
                       QStringLiteral("Word activation: %1 chars at cursor").arg(wordLen));
            return true;
        }
    }

    return false;
}

}  // namespace qcai2
