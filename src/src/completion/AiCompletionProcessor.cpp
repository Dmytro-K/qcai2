/*! @file
    @brief Implements asynchronous AI completion proposal generation.
*/

#include "AiCompletionProcessor.h"
#include "../context/ChatContextManager.h"
#include "../models/AgentMessages.h"
#include "../providers/IAIProvider.h"
#include "../settings/Settings.h"
#include "../util/Logger.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>

#include <QTextDocument>

#include <functional>

namespace qcai2
{

static const int kContextBefore = 2000;  // chars before cursor
static const int kContextAfter = 500;    // chars after cursor

AiCompletionProcessor::AiCompletionProcessor(IAIProvider *provider,
                                             ChatContextManager *chatContextManager,
                                             const QString &model)
    : m_provider(provider), m_model(model), m_chatContextManager(chatContextManager)
{
}

AiCompletionProcessor::~AiCompletionProcessor()
{
    *m_alive = false;
}

bool AiCompletionProcessor::running()
{
    return m_running;
}

void AiCompletionProcessor::cancel()
{
    m_cancelled = true;
    m_running = false;
    // Don't call m_provider->cancel() — it can trigger synchronous callbacks
    // that cause re-entrancy crash in Qt Creator's CodeAssistantPrivate::cancelCurrentRequest()
}

TextEditor::IAssistProposal *AiCompletionProcessor::perform()
{
    if ((m_provider == nullptr) || m_cancelled)
    {
        return nullptr;
    }

    const TextEditor::AssistInterface *iface = interface();
    if (iface == nullptr)
    {
        return nullptr;
    }

    const int pos = iface->position();
    const QTextDocument *doc = iface->textDocument();
    if (doc == nullptr)
    {
        return nullptr;
    }

    // Extract text before and after cursor
    const QString fullText = doc->toPlainText();
    const int startBefore = qMax(0, pos - kContextBefore);
    const QString prefix = fullText.mid(startBefore, pos - startBefore);
    const QString suffix = fullText.mid(pos, qMin(kContextAfter, fullText.length() - pos));
    const QString fileName = iface->filePath().toUrlishString();

    // Build FIM (fill-in-the-middle) prompt
    const QString prompt =
        QStringLiteral("You are a code completion engine. Complete the code at the cursor "
                       "position marked <CURSOR>.\n"
                       "File: %1\n"
                       "Return ONLY the completion text, no explanation, no markdown.\n\n"
                       "```\n%2<CURSOR>%3\n```")
            .arg(fileName, prefix, suffix);

    QList<ChatMessage> messages;
    messages.append({QStringLiteral("system"),
                     QStringLiteral("You are a code completion assistant. "
                                    "Return only the code that should be inserted at the cursor. "
                                    "No explanations, no markdown fences, no comments. "
                                    "Keep completions short (1-5 lines).")});
    if (m_chatContextManager != nullptr)
    {
        QString contextError;
        const QString completionContext =
            m_chatContextManager->buildCompletionContextBlock(fileName, 128, &contextError);
        if (contextError.isEmpty() == false)
        {
            QCAI_WARN("Completion",
                      QStringLiteral("Failed to build completion context: %1").arg(contextError));
        }
        else if (completionContext.isEmpty() == false)
        {
            messages.append({QStringLiteral("system"), completionContext});
        }
    }
    messages.append({QStringLiteral("user"), prompt});

    m_running = true;
    QCAI_DEBUG("Completion",
               QStringLiteral(
                   "Requesting completion at pos %1 in %2 (prefix: %3 chars, suffix: %4 chars)")
                   .arg(pos)
                   .arg(fileName)
                   .arg(prefix.length())
                   .arg(suffix.length()));

    using namespace std::placeholders;
    const auto alive = m_alive;
    const IAIProvider::CompletionCallback completionCallback = std::bind(
        &AiCompletionProcessor::dispatchCompletionResponse, this, pos, alive, _1, _2, _3);
    m_provider->complete(messages, m_model, 0.0, 128, settings().completionReasoningEffort,
                         completionCallback);

    return nullptr;  // async — proposal delivered via setAsyncProposalAvailable
}

void AiCompletionProcessor::dispatchCompletionResponse(AiCompletionProcessor *processor, int pos,
                                                       const std::shared_ptr<bool> &alive,
                                                       const QString &response,
                                                       const QString &error,
                                                       const ProviderUsage &usage)
{
    Q_UNUSED(usage);

    if (*alive == false)
    {
        return;
    }

    processor->handleCompletionResponse(pos, response, error);
}

void AiCompletionProcessor::handleCompletionResponse(int pos, const QString &response,
                                                     const QString &error)
{
    m_running = false;

    if (m_cancelled || !error.isEmpty() || response.trimmed().isEmpty())
    {
        if (!error.isEmpty())
        {
            QCAI_WARN("Completion", QStringLiteral("Completion error: %1").arg(error));
        }
        else if (m_cancelled)
        {
            QCAI_DEBUG("Completion", QStringLiteral("Completion cancelled"));
        }
        setAsyncProposalAvailable(nullptr);
        return;
    }

    QString completion = response.trimmed();
    if (completion.startsWith(QStringLiteral("```")))
    {
        const qsizetype firstNl = completion.indexOf(QLatin1Char('\n'));
        if (firstNl >= 0)
        {
            completion = completion.mid(firstNl + 1);
        }
        if (completion.endsWith(QStringLiteral("```")))
        {
            completion.chop(3);
        }
        completion = completion.trimmed();
    }

    if (completion.isEmpty() == true)
    {
        setAsyncProposalAvailable(nullptr);
        return;
    }

    QList<TextEditor::AssistProposalItemInterface *> items;

    auto *item = new TextEditor::AssistProposalItem;
    const qsizetype nlPos = completion.indexOf(QLatin1Char('\n'));
    if (nlPos > 0)
    {
        item->setText(completion.left(nlPos));
        item->setDetail(completion);
    }
    else
    {
        item->setText(completion);
        item->setDetail(QStringLiteral("AI completion"));
    }
    item->setData(completion);
    items.append(item);

    if (nlPos > 0)
    {
        auto *lineItem = new TextEditor::AssistProposalItem;
        lineItem->setText(completion.left(nlPos));
        lineItem->setDetail(QStringLiteral("AI (first line)"));
        lineItem->setData(completion.left(nlPos));
        items.append(lineItem);
    }

    auto *proposal = new TextEditor::GenericProposal(pos, items);
    QCAI_DEBUG("Completion", QStringLiteral("Completion ready: %1 items, %2 chars")
                                 .arg(items.size())
                                 .arg(completion.length()));
    setAsyncProposalAvailable(proposal);
}

}  // namespace qcai2
