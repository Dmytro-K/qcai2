/*! Declares the `/compact` slash command — asks the AI to compress the current
 *  conversation into a short structured summary for future context.
 */

#include "slash_command_registry.h"

#include <QString>

namespace qcai2
{

QString compact_prompt_text(const QString &focus)
{
    QString prompt = QStringLiteral(
        "Збережи тільки інформацію, яка реально важлива для наступних кроків.\n"
        "\n"
        "Завдання:\n"
        "1. Збережи тільки інформацію, яка реально важлива для наступних кроків.\n"
        "2. Прибери шум, повтори, дрібні уточнення, тимчасові деталі, які вже не потрібні.\n"
        "3. Перетвори стару історію на короткий, структурований summary.\n"
        "4. Винеси довгоживучі факти в окремий список memory items.\n"
        "5. Винеси великі outputs (логи, дифи, результати тестів, tool outputs) в "
        "artifacts/references, а не вставляй їх повністю.\n"
        "6. Залиш останні релевантні повідомлення окремо від summary.\n"
        "7. Не втрать:\n"
        "   - поточну ціль користувача\n"
        "   - важливі технічні рішення\n"
        "   - обмеження\n"
        "   - невирішені проблеми\n"
        "   - результати попередніх кроків\n"
        "   - важливі шляхи, файли, команди, конфіги, ідентифікатори\n"
        "\n"
        "Поверни результат у такому форматі:\n"
        "\n"
        "## Current Goal\n"
        "Коротко: яка зараз головна ціль.\n"
        "\n"
        "## Stable Memory\n"
        "- довгоживучі факти\n"
        "- налаштування\n"
        "- уподобання\n"
        "- важливі обмеження\n"
        "\n"
        "## Key Decisions\n"
        "- прийняті технічні рішення\n"
        "- що вже вирішено і чому\n"
        "\n"
        "## Open Problems\n"
        "- що ще не вирішено\n"
        "- що блокує рух далі\n"
        "\n"
        "## Recent Relevant Context\n"
        "- коротко про останні важливі кроки\n"
        "\n"
        "## Artifacts\n"
        "- логи, дифи, результати тестів, outputs інструментів\n"
        "- тільки короткий опис + посилання/ідентифікатори, без повного вмісту\n"
        "\n"
        "## Dropped / Safe To Forget\n"
        "- що можна більше не тримати в активному контексті\n"
        "\n"
        "Правила:\n"
        "- Будь максимально стислим, але не втрачай важливого.\n"
        "- Не переписуй усе підряд.\n"
        "- Не вигадуй нових фактів.\n"
        "- Якщо є невизначеність, явно познач її.\n"
        "- Орієнтуйся на те, що цей compact буде використаний як контекст для "
        "наступних запитів агента.");

    if (focus.isEmpty() == false)
    {
        prompt += QStringLiteral("\n\nДодатковий фокус для compact: %1").arg(focus.trimmed());
    }

    return prompt;
}

void compact_command(const slash_command_invocation_t &invocation,
                     const slash_command_context_t &context)
{
    Q_UNUSED(context);

    if (context.goal_override != nullptr)
    {
        *context.goal_override = compact_prompt_text(invocation.arguments);
    }
}

DECLARE_COMMAND(compact, "Compress the relevant conversation context into a short summary.",
                compact_command)

}  // namespace qcai2
