/*! @file
    @brief Implements debounced inline ghost-text completion requests.
*/

#include "ghost_text_manager.h"
#include "../context/chat_context_manager.h"
#include "../models/agent_messages.h"
#include "../providers/iai_provider.h"
#include "../settings/settings.h"
#include "../util/logger.h"
#include "../util/project_root_resolver.h"
#include "../util/prompt_instructions.h"

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textsuggestion.h>
#include <utils/textutils.h>

#include <QMetaObject>
#include <QTextCursor>
#include <QTextDocument>

#include <functional>

namespace qcai2
{

static const int kContextBefore = 2000;
static const int kContextAfter = 500;
static const int kDebounceMs = 500;

ghost_text_manager_t::ghost_text_manager_t(QObject *parent) : QObject(parent)
{
}

ghost_text_manager_t::~ghost_text_manager_t()
{
    *this->alive = false;
}

void ghost_text_manager_t::set_enabled(bool enabled)
{
    this->enabled = enabled;
}

void ghost_text_manager_t::attach_to_editor(TextEditor::TextEditorWidget *editor)
{
    if ((((editor == nullptr) || this->debounce_timers.contains(editor)) == true))
    {
        return;
    }

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kDebounceMs);
    connect(timer, &QTimer::timeout, this, [this, editor]() { this->request_completion(editor); });
    this->debounce_timers.insert(editor, timer);
    this->last_edit_positions.insert(editor, editor->textCursor().position());

    connect(editor->document(), &QTextDocument::contentsChange, this,
            [this, editor](int position, int charsRemoved, int charsAdded) {
                if (((charsRemoved == 0 && charsAdded == 0) == true))
                {
                    return;
                }
                // Only trigger when the cursor is right after the edit (user is typing/pasting).
                // Background reformatting fires at a different position, so this filters it out.
                if (editor->textCursor().position() != position + charsAdded)
                {
                    return;
                }
                this->on_contents_changed(editor);
            });

    connect(editor, &QObject::destroyed, this, [this, editor]() {
        if (auto *t = this->debounce_timers.take(editor); ((t != nullptr) == true))
        {
            t->deleteLater();
        }
        this->last_edit_positions.remove(editor);
    });

    QCAI_DEBUG("GhostText", QStringLiteral("Attached to editor"));
}

void ghost_text_manager_t::on_contents_changed(TextEditor::TextEditorWidget *editor)
{
    if (((!this->enabled || (this->provider == nullptr)) == true))
    {
        return;
    }

    const auto &s = settings();
    if (s.ai_completion_enabled == false)
    {
        return;
    }

    this->last_edit_positions.insert(editor, editor->textCursor().position());
    if (auto *timer = this->debounce_timers.value(editor); ((timer != nullptr) == true))
    {
        timer->start();
    }
}

void ghost_text_manager_t::request_completion(TextEditor::TextEditorWidget *editor)
{
    if (((!this->enabled || (this->provider == nullptr) || (editor == nullptr)) == true))
    {
        return;
    }

    const auto &s = settings();
    if (s.ai_completion_enabled == false)
    {
        return;
    }

    QTextCursor tc = editor->textCursor();
    const int pos = tc.position();
    if (((pos != this->last_edit_positions.value(editor, pos)) == true))
    {
        return;
    }

    const QTextDocument *doc = editor->document();
    if (((doc == nullptr) == true))
    {
        return;
    }

    const QString fullText = doc->toPlainText();
    const int startBefore = qMax(0, pos - kContextBefore);
    const QString prefix = fullText.mid(startBefore, pos - startBefore);
    const QString suffix = fullText.mid(pos, qMin(kContextAfter, fullText.length() - pos));
    const QString fileName = (editor->textDocument() != nullptr)
                                 ? editor->textDocument()->filePath().toUrlishString()
                                 : QStringLiteral("untitled");
    const QString project_root = project_root_for_file_path(fileName);

    const QString model = s.completion_model.isEmpty() ? this->model : s.completion_model;

    const QString prompt =
        QStringLiteral("You are a code completion engine. Complete the code at the cursor "
                       "position marked <CURSOR>.\n"
                       "File: %1\n"
                       "Return ONLY the completion text, no explanation, no markdown.\n\n"
                       "```\n%2<CURSOR>%3\n```")
            .arg(fileName, prefix, suffix);

    QList<chat_message_t> messages;
    QString instruction_error;
    append_configured_system_instructions(&messages, project_root, settings().system_prompt,
                                          &instruction_error);
    if (instruction_error.isEmpty() == false)
    {
        QCAI_WARN("GhostText",
                  QStringLiteral("Failed to load project rules: %1").arg(instruction_error));
    }
    messages.append({QStringLiteral("system"),
                     QStringLiteral("You are a code completion assistant. "
                                    "Return only the code that should be inserted at the cursor. "
                                    "No explanations, no markdown fences, no comments. "
                                    "Keep completions short (1-5 lines).")});
    if (this->chat_context_manager != nullptr)
    {
        QString contextError;
        const QString completionContext =
            this->chat_context_manager->build_completion_context_block(fileName, 128,
                                                                       &contextError);
        if (contextError.isEmpty() == false)
        {
            QCAI_WARN("GhostText",
                      QStringLiteral("Failed to build completion context: %1").arg(contextError));
        }
        else if (completionContext.isEmpty() == false)
        {
            messages.append({QStringLiteral("system"), completionContext});
        }
    }
    messages.append({QStringLiteral("user"), prompt});

    QCAI_DEBUG("GhostText", QStringLiteral("Requesting completion at pos %1").arg(pos));

    using namespace std::placeholders;
    const auto alive = this->alive;
    const QPointer<TextEditor::TextEditorWidget> editorGuard(editor);
    const iai_provider_t::completion_callback_t completion_callback =
        std::bind(&ghost_text_manager_t::dispatch_completion_response, this, editorGuard, pos,
                  alive, _1, _2, _3);
    this->provider->complete(messages, model, 0.0, 128, settings().completion_reasoning_effort,
                             completion_callback);
}

void ghost_text_manager_t::dispatch_completion_response(
    ghost_text_manager_t *manager, QPointer<TextEditor::TextEditorWidget> editor, int pos,
    const std::shared_ptr<bool> &alive, const QString &response, const QString &error,
    const provider_usage_t &usage)
{
    Q_UNUSED(usage);

    if ((*alive == false) || (editor.isNull() == true))
    {
        return;
    }

    manager->handle_completion_response(editor.data(), pos, response, error);
}

void ghost_text_manager_t::handle_completion_response(TextEditor::TextEditorWidget *editor,
                                                      int pos, const QString &response,
                                                      const QString &error)
{
    if (error.isEmpty() == false)
    {
        QCAI_WARN("GhostText", QStringLiteral("Error: %1").arg(error));
        return;
    }

    const QString completion = this->clean_completion(response);
    if (completion.isEmpty() == true)
    {
        return;
    }

    if (editor->textCursor().position() != pos)
    {
        return;
    }

    QTextCursor tc = editor->textCursor();
    const int line = tc.blockNumber() + 1;
    const int col = tc.positionInBlock();

    const QString lineText = tc.block().text();
    const QString linePrefix = lineText.left(col);
    const QString lineSuffix = lineText.mid(col);
    const QString replacementText = linePrefix + completion + lineSuffix;

    Utils::Text::Position textPos{line, col};
    Utils::Text::Range range{textPos, textPos};
    TextEditor::TextSuggestion::Data data{range, textPos, replacementText};

    editor->insertSuggestion(
        std::make_unique<TextEditor::TextSuggestion>(data, editor->document()));

    QCAI_DEBUG("GhostText",
               QStringLiteral("Showing suggestion: %1 chars").arg(completion.length()));
}

QString ghost_text_manager_t::clean_completion(const QString &raw)
{
    QString text = raw.trimmed();
    if (((text.startsWith(QStringLiteral("```"))) == true))
    {
        qsizetype firstNl = text.indexOf(QLatin1Char('\n'));
        if (firstNl >= 0)
        {
            text = text.mid(firstNl + 1);
        }
        if (((text.endsWith(QStringLiteral("```"))) == true))
        {
            text.chop(3);
        }
        text = text.trimmed();
    }
    return text;
}

}  // namespace qcai2
