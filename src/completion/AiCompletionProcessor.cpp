#include "AiCompletionProcessor.h"
#include "../models/AgentMessages.h"
#include "../providers/IAIProvider.h"
#include "../settings/Settings.h"
#include "../util/Logger.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>

#include <QTextDocument>

namespace qcai2
{

static const int kContextBefore = 2000;  // chars before cursor
static const int kContextAfter = 500;    // chars after cursor

AiCompletionProcessor::AiCompletionProcessor(IAIProvider *provider, const QString &model)
    : m_provider(provider), m_model(model)
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
    if (!m_provider || m_cancelled)
        return nullptr;

    const TextEditor::AssistInterface *iface = interface();
    if (!iface)
        return nullptr;

    const int pos = iface->position();
    const QTextDocument *doc = iface->textDocument();
    if (!doc)
        return nullptr;

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
    messages.append({QStringLiteral("user"), prompt});

    m_running = true;
    QCAI_DEBUG("Completion",
               QStringLiteral(
                   "Requesting completion at pos %1 in %2 (prefix: %3 chars, suffix: %4 chars)")
                   .arg(pos)
                   .arg(fileName)
                   .arg(prefix.length())
                   .arg(suffix.length()));

    // Async: send request, deliver proposal when ready
    auto alive = m_alive;  // capture shared_ptr for safe use-after-free check
    m_provider->complete(
        messages, m_model, 0.0, 128, settings().completionReasoningEffort,
        [this, pos, alive](const QString &response, const QString &error) {
            if (!*alive)
                return;  // processor was destroyed, bail out

            m_running = false;

            if (m_cancelled || !error.isEmpty() || response.trimmed().isEmpty())
            {
                if (!error.isEmpty())
                    QCAI_WARN("Completion", QStringLiteral("Completion error: %1").arg(error));
                else if (m_cancelled)
                    QCAI_DEBUG("Completion", QStringLiteral("Completion cancelled"));
                setAsyncProposalAvailable(nullptr);
                return;
            }

            // Clean up response — remove markdown fences if the model added them
            QString completion = response.trimmed();
            if (completion.startsWith(QStringLiteral("```")))
            {
                qsizetype firstNl = completion.indexOf(QLatin1Char('\n'));
                if (firstNl >= 0)
                    completion = completion.mid(firstNl + 1);
                if (completion.endsWith(QStringLiteral("```")))
                    completion.chop(3);
                completion = completion.trimmed();
            }

            if (completion.isEmpty())
            {
                setAsyncProposalAvailable(nullptr);
                return;
            }

            // Split into individual line suggestions
            QList<TextEditor::AssistProposalItemInterface *> items;

            // Full completion as primary item
            auto *item = new TextEditor::AssistProposalItem;
            // Show first line as label, full text as detail
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

            // If multi-line, also offer just the first line
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
        });

    return nullptr;  // async — proposal delivered via setAsyncProposalAvailable
}

}  // namespace qcai2
