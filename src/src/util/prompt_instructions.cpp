#include "prompt_instructions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{

QString normalized_instruction_text(const QString &text)
{
    return text.trimmed();
}

}  // namespace

namespace qcai2
{

QString project_rules_file_path(const QString &project_root)
{
    if (project_root.trimmed().isEmpty())
    {
        return {};
    }
    return QDir(project_root).filePath(".qcai2/rules.md");
}

QString project_session_state_file_path(const QString &project_root)
{
    if (project_root.trimmed().isEmpty())
    {
        return {};
    }
    return QDir(project_root).filePath(".qcai2/session.json");
}

bool project_ignores_global_system_prompt(const QString &project_root, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    const QString session_path = project_session_state_file_path(project_root);
    if (session_path.isEmpty())
    {
        return false;
    }

    QFile file(session_path);
    if (file.exists() == false)
    {
        return false;
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        if (error != nullptr)
        {
            *error = file.errorString();
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Invalid JSON object in %1").arg(session_path);
        }
        return false;
    }

    return document.object().value(QStringLiteral("ignoreGlobalSystemPrompt")).toBool(false);
}

QString read_project_rules(const QString &project_root, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    const QString rules_path = project_rules_file_path(project_root);
    if (rules_path.isEmpty())
    {
        return {};
    }

    QFile file(rules_path);
    if (!file.exists())
    {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error != nullptr)
        {
            *error = file.errorString();
        }
        return {};
    }
    return normalized_instruction_text(QString::fromUtf8(file.readAll()));
}

QStringList configured_system_instructions(const QString &project_root,
                                           const QString &global_prompt, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    QStringList instructions;

    QString ignore_error;
    const bool ignore_global_prompt =
        project_ignores_global_system_prompt(project_root, &ignore_error);
    const QString normalized_global_prompt = normalized_instruction_text(global_prompt);
    if (ignore_global_prompt == false && !normalized_global_prompt.isEmpty())
    {
        instructions.append(normalized_global_prompt);
    }

    QString rules_error;
    const QString project_rules = read_project_rules(project_root, &rules_error);
    if (!project_rules.isEmpty())
    {
        instructions.append(project_rules);
    }

    if (error != nullptr)
    {
        if (ignore_error.isEmpty() == false)
        {
            *error = ignore_error;
        }
        else if (rules_error.isEmpty() == false)
        {
            *error = rules_error;
        }
    }

    return instructions;
}

void append_configured_system_instructions(QList<chat_message_t> *messages,
                                           const QString &project_root,
                                           const QString &global_prompt, QString *error)
{
    if (messages == nullptr)
    {
        return;
    }

    const QStringList instructions =
        configured_system_instructions(project_root, global_prompt, error);
    for (const QString &instruction : instructions)
    {
        messages->append(chat_message_t{QStringLiteral("system"), instruction});
    }
}

}  // namespace qcai2
