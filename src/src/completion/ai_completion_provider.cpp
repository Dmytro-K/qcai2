/*! @file
    @brief Implements the AI completion assist provider.
*/

#include "ai_completion_provider.h"
#include "../context/chat_context_manager.h"
#include "../providers/iai_provider.h"
#include "../settings/settings.h"
#include "../util/logger.h"
#include "ai_completion_processor.h"
#include "completion_request_settings.h"
#include "completion_trigger_utils.h"

#include <texteditor/texteditor.h>

#include <QTextCursor>

namespace qcai2
{

namespace
{

iai_provider_t *find_provider_by_id(const QList<iai_provider_t *> &providers, const QString &id)
{
    for (iai_provider_t *provider : providers)
    {
        if (provider != nullptr && provider->id() == id)
        {
            return provider;
        }
    }
    return nullptr;
}

}  // namespace

ai_completion_provider_t::ai_completion_provider_t(QObject *parent)
    : CompletionAssistProvider(parent)
{
}

iai_provider_t *ai_completion_provider_t::resolve_provider() const
{
    const completion_request_settings_t completion_settings =
        resolve_completion_request_settings(settings());
    if (iai_provider_t *resolved =
            find_provider_by_id(this->providers, completion_settings.provider_id);
        resolved != nullptr)
    {
        return resolved;
    }
    if (this->provider != nullptr)
    {
        return this->provider;
    }
    return this->providers.isEmpty() == false ? this->providers.first() : nullptr;
}

TextEditor::IAssistProcessor *ai_completion_provider_t::createProcessor(
    const TextEditor::AssistInterface * /*assistInterface*/) const
{
    const auto &s = settings();
    iai_provider_t *provider = this->resolve_provider();
    if (((!s.ai_completion_enabled || (provider == nullptr)) == true))
    {
        return nullptr;
    }

    const completion_request_settings_t completion_settings =
        resolve_completion_request_settings(s);
    QCAI_DEBUG(
        "Completion",
        QStringLiteral("createProcessor: provider='%1' completionProvider='%2' model='%3'")
            .arg(provider->id(), completion_settings.provider_id, completion_settings.model));
    return new ai_completion_processor_t(provider, this->chat_context_manager,
                                         completion_settings.model);
}

int ai_completion_provider_t::activationCharSequenceLength() const
{
    return 1;
}

bool ai_completion_provider_t::isActivationCharSequence(const QString &sequence) const
{
    if (((sequence.isEmpty() || (this->resolve_provider() == nullptr)) == true))
    {
        return false;
    }

    const auto &s = settings();
    if (s.ai_completion_enabled == false)
    {
        return false;
    }

    if (sequence.isEmpty() == false)
    {
        auto *editor = TextEditor::TextEditorWidget::currentTextEditorWidget();
        if (((editor == nullptr) == true))
        {
            return false;
        }

        QTextCursor tc = editor->textCursor();
        const QString line_text = tc.block().text();
        const int column = tc.positionInBlock();
        if (should_auto_trigger_completion(line_text, column, s.completion_min_chars) == true)
        {
            QCAI_DEBUG("Completion",
                       QStringLiteral("Auto-trigger activation: %1 chars at cursor")
                           .arg(identifier_fragment_length_before_cursor(line_text, column)));
            return true;
        }
    }

    return false;
}

}  // namespace qcai2
