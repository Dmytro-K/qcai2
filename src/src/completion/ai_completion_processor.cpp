/*! @file
    @brief Implements asynchronous AI completion proposal generation.
*/

#include "ai_completion_processor.h"
#include "../context/chat_context_manager.h"
#include "../models/agent_messages.h"
#include "../providers/iai_provider.h"
#include "../settings/settings.h"
#include "../util/logger.h"
#include "../util/project_root_resolver.h"
#include "../util/prompt_instructions.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>

#include <QTextDocument>

#include <functional>

namespace qcai2
{

static const int k_context_before = 2000;  // chars before cursor
static const int k_context_after = 500;    // chars after cursor

ai_completion_processor_t::ai_completion_processor_t(iai_provider_t *provider,
                                                     chat_context_manager_t *chat_context_manager,
                                                     const QString &model)
    : provider(provider), model(model), chat_context_manager(chat_context_manager)
{
}

ai_completion_processor_t::~ai_completion_processor_t()
{
    *this->alive = false;
}

bool ai_completion_processor_t::running()
{
    return this->request_running;
}

void ai_completion_processor_t::cancel()
{
    this->cancelled = true;
    this->request_running = false;
    // Don't call this->provider_->cancel() — it can trigger synchronous callbacks
    // that cause re-entrancy crash in Qt Creator's CodeAssistantPrivate::cancelCurrentRequest()
}

TextEditor::IAssistProposal *ai_completion_processor_t::perform()
{
    if ((this->provider == nullptr) || this->cancelled)
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
    const QString full_text = doc->toPlainText();
    const int start_before = qMax(0, pos - k_context_before);
    const QString prefix = full_text.mid(start_before, pos - start_before);
    const QString suffix = full_text.mid(pos, qMin(k_context_after, full_text.length() - pos));
    const QString file_name = iface->filePath().toUrlishString();
    const QString project_root = project_root_for_file_path(file_name);

    // Build FIM (fill-in-the-middle) prompt
    const QString prompt =
        QStringLiteral("You are a code completion engine. Complete the code at the cursor "
                       "position marked <CURSOR>.\n"
                       "File: %1\n"
                       "Return ONLY the completion text, no explanation, no markdown.\n\n"
                       "```\n%2<CURSOR>%3\n```")
            .arg(file_name, prefix, suffix);

    QList<chat_message_t> messages;
    QString instruction_error;
    append_configured_system_instructions(&messages, project_root, settings().system_prompt,
                                          &instruction_error);
    if (instruction_error.isEmpty() == false)
    {
        QCAI_WARN("Completion",
                  QStringLiteral("Failed to load project rules: %1").arg(instruction_error));
    }
    messages.append({QStringLiteral("system"),
                     QStringLiteral("You are a code completion assistant. "
                                    "Return only the code that should be inserted at the cursor. "
                                    "No explanations, no markdown fences, no comments. "
                                    "Keep completions short (1-5 lines).")});
    if (this->chat_context_manager != nullptr)
    {
        QString context_error;
        const QString completion_context =
            this->chat_context_manager->build_completion_context_block(file_name, 128,
                                                                       &context_error);
        if (context_error.isEmpty() == false)
        {
            QCAI_WARN("Completion",
                      QStringLiteral("Failed to build completion context: %1").arg(context_error));
        }
        else if (completion_context.isEmpty() == false)
        {
            messages.append({QStringLiteral("system"), completion_context});
        }
    }
    messages.append({QStringLiteral("user"), prompt});

    this->request_running = true;
    QCAI_DEBUG("Completion",
               QStringLiteral(
                   "Requesting completion at pos %1 in %2 (prefix: %3 chars, suffix: %4 chars)")
                   .arg(pos)
                   .arg(file_name)
                   .arg(prefix.length())
                   .arg(suffix.length()));

    using namespace std::placeholders;
    const auto alive = this->alive;
    const iai_provider_t::completion_callback_t completion_callback = std::bind(
        &ai_completion_processor_t::dispatch_completion_response, this, pos, alive, _1, _2, _3);
    this->provider->complete(messages, this->model, 0.0, 128,
                             settings().completion_reasoning_effort, completion_callback);

    return nullptr;  // async — proposal delivered via setAsyncProposalAvailable
}

void ai_completion_processor_t::dispatch_completion_response(
    ai_completion_processor_t *processor, int pos, const std::shared_ptr<bool> &alive,
    const QString &response, const QString &error, const provider_usage_t &usage)
{
    Q_UNUSED(usage);

    if (*alive == false)
    {
        return;
    }

    processor->handle_completion_response(pos, response, error);
}

void ai_completion_processor_t::handle_completion_response(int pos, const QString &response,
                                                           const QString &error)
{
    this->request_running = false;

    if (this->cancelled || !error.isEmpty() || response.trimmed().isEmpty())
    {
        if (!error.isEmpty())
        {
            QCAI_WARN("Completion", QStringLiteral("Completion error: %1").arg(error));
        }
        else if (this->cancelled)
        {
            QCAI_DEBUG("Completion", QStringLiteral("Completion cancelled"));
        }
        setAsyncProposalAvailable(nullptr);
        return;
    }

    QString completion = response.trimmed();
    if (completion.startsWith(QStringLiteral("```")))
    {
        const qsizetype first_nl = completion.indexOf(QLatin1Char('\n'));
        if (first_nl >= 0)
        {
            completion = completion.mid(first_nl + 1);
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
    const qsizetype nl_pos = completion.indexOf(QLatin1Char('\n'));
    if (nl_pos > 0)
    {
        item->setText(completion.left(nl_pos));
        item->setDetail(completion);
    }
    else
    {
        item->setText(completion);
        item->setDetail(QStringLiteral("AI completion"));
    }
    item->setData(completion);
    items.append(item);

    if (nl_pos > 0)
    {
        auto *line_item = new TextEditor::AssistProposalItem;
        line_item->setText(completion.left(nl_pos));
        line_item->setDetail(QStringLiteral("AI (first line)"));
        line_item->setData(completion.left(nl_pos));
        items.append(line_item);
    }

    auto *proposal = new TextEditor::GenericProposal(pos, items);
    QCAI_DEBUG("Completion", QStringLiteral("Completion ready: %1 items, %2 chars")
                                 .arg(items.size())
                                 .arg(completion.length()));
    setAsyncProposalAvailable(proposal);
}

}  // namespace qcai2
