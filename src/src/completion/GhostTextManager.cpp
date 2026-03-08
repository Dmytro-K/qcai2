/*! @file
    @brief Implements debounced inline ghost-text completion requests.
*/

#include "GhostTextManager.h"
#include "../models/AgentMessages.h"
#include "../providers/IAIProvider.h"
#include "../settings/Settings.h"
#include "../util/Logger.h"

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textsuggestion.h>
#include <utils/textutils.h>

#include <QMetaObject>
#include <QTextCursor>
#include <QTextDocument>

namespace qcai2
{

static const int kContextBefore = 2000;
static const int kContextAfter = 500;
static const int kDebounceMs = 500;

GhostTextManager::GhostTextManager(QObject *parent) : QObject(parent)
{
}

void GhostTextManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void GhostTextManager::attachToEditor(TextEditor::TextEditorWidget *editor)
{
    if (!editor || m_debounceTimers.contains(editor))
        return;

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kDebounceMs);
    connect(timer, &QTimer::timeout, this, [this, editor]() { requestCompletion(editor); });
    m_debounceTimers.insert(editor, timer);
    m_lastEditPositions.insert(editor, editor->textCursor().position());

    connect(editor->document(), &QTextDocument::contentsChange, this,
            [this, editor](int position, int charsRemoved, int charsAdded) {
                if (charsRemoved == 0 && charsAdded == 0)
                    return;
                // Only trigger when the cursor is right after the edit (user is typing/pasting).
                // Background reformatting fires at a different position, so this filters it out.
                if (editor->textCursor().position() != position + charsAdded)
                    return;
                onContentsChanged(editor);
            });

    connect(editor, &QObject::destroyed, this, [this, editor]() {
        if (auto *t = m_debounceTimers.take(editor))
            t->deleteLater();
        m_lastEditPositions.remove(editor);
    });

    QCAI_DEBUG("GhostText", QStringLiteral("Attached to editor"));
}

void GhostTextManager::onContentsChanged(TextEditor::TextEditorWidget *editor)
{
    if (!m_enabled || !m_provider)
        return;

    const auto &s = settings();
    if (!s.aiCompletionEnabled)
        return;

    m_lastEditPositions.insert(editor, editor->textCursor().position());
    if (auto *timer = m_debounceTimers.value(editor))
        timer->start();
}

void GhostTextManager::requestCompletion(TextEditor::TextEditorWidget *editor)
{
    if (!m_enabled || !m_provider || !editor)
        return;

    const auto &s = settings();
    if (!s.aiCompletionEnabled)
        return;

    QTextCursor tc = editor->textCursor();
    const int pos = tc.position();
    if (pos != m_lastEditPositions.value(editor, pos))
        return;

    const QTextDocument *doc = editor->document();
    if (!doc)
        return;

    const QString fullText = doc->toPlainText();
    const int startBefore = qMax(0, pos - kContextBefore);
    const QString prefix = fullText.mid(startBefore, pos - startBefore);
    const QString suffix = fullText.mid(pos, qMin(kContextAfter, fullText.length() - pos));
    const QString fileName = editor->textDocument()
                                 ? editor->textDocument()->filePath().toUrlishString()
                                 : QStringLiteral("untitled");

    const QString model = s.completionModel.isEmpty() ? m_model : s.completionModel;

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

    QCAI_DEBUG("GhostText", QStringLiteral("Requesting completion at pos %1").arg(pos));

    auto alive = m_alive;
    m_provider->complete(
        messages, model, 0.0, 128, settings().completionReasoningEffort,
        [this, editor, pos, alive](const QString &response, const QString &error) {
            if (!*alive)
                return;
            if (!error.isEmpty())
            {
                QCAI_WARN("GhostText", QStringLiteral("Error: %1").arg(error));
                return;
            }

            const QString completion = cleanCompletion(response);
            if (completion.isEmpty())
                return;

            // Verify cursor hasn't moved
            if (editor->textCursor().position() != pos)
                return;

            QTextCursor tc = editor->textCursor();
            const int line = tc.blockNumber() + 1;  // 1-based
            const int col = tc.positionInBlock();   // 0-based

            // Build full replacement text: prefix (existing text before cursor) + completion +
            // suffix (rest of line)
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
        });
}

QString GhostTextManager::cleanCompletion(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.startsWith(QStringLiteral("```")))
    {
        qsizetype firstNl = text.indexOf(QLatin1Char('\n'));
        if (firstNl >= 0)
            text = text.mid(firstNl + 1);
        if (text.endsWith(QStringLiteral("```")))
            text.chop(3);
        text = text.trimmed();
    }
    return text;
}

}  // namespace qcai2
