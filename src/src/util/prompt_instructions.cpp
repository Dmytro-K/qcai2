#include "prompt_instructions.h"

#include <QDir>
#include <QDirIterator>
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

QStringList standard_instruction_file_paths(const QString &project_root,
                                            const qcai2::prompt_instruction_options_t &options)
{
    if (project_root.trimmed().isEmpty())
    {
        return {};
    }

    QDir project_dir(project_root);
    QStringList paths;
    if (options.load_agents_md == true)
    {
        paths.append(project_dir.filePath(QStringLiteral("AGENTS.md")));
    }
    if (options.load_github_copilot_instructions == true)
    {
        paths.append(project_dir.filePath(QStringLiteral(".github/copilot-instructions.md")));
    }
    if (options.load_claude_md == true)
    {
        paths.append(project_dir.filePath(QStringLiteral("CLAUDE.md")));
    }
    if (options.load_gemini_md == true)
    {
        paths.append(project_dir.filePath(QStringLiteral("GEMINI.md")));
    }

    const QString instructions_dir = project_dir.filePath(QStringLiteral(".github/instructions"));
    if (options.load_github_instructions_dir == true &&
        QFileInfo::exists(instructions_dir) == true)
    {
        QStringList nested_instruction_paths;
        QDirIterator it(instructions_dir, QStringList{QStringLiteral("*.instructions.md")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() == true)
        {
            nested_instruction_paths.append(it.next());
        }
        nested_instruction_paths.sort();
        paths.append(nested_instruction_paths);
    }

    return paths;
}

QStringList read_standard_instruction_files(const QString &project_root,
                                            const qcai2::prompt_instruction_options_t &options,
                                            QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    QStringList instructions;
    QString first_error;
    QStringList seen_paths;
    const QStringList candidate_paths = standard_instruction_file_paths(project_root, options);
    for (const QString &path : candidate_paths)
    {
        const QString canonical_path = QFileInfo(path).canonicalFilePath();
        const QString dedup_path = canonical_path.isEmpty() == false ? canonical_path : path;
        if (seen_paths.contains(dedup_path) == true)
        {
            continue;
        }
        seen_paths.append(dedup_path);

        QFile file(path);
        if (file.exists() == false)
        {
            continue;
        }
        if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
        {
            if (first_error.isEmpty() == true)
            {
                first_error = QStringLiteral("%1: %2").arg(path, file.errorString());
            }
            continue;
        }

        const QString text = normalized_instruction_text(QString::fromUtf8(file.readAll()));
        if (text.isEmpty() == false)
        {
            instructions.append(text);
        }
    }

    if (error != nullptr)
    {
        *error = first_error;
    }
    return instructions;
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

QString autocomplete_prompt_file_path(const QString &project_root)
{
    if (project_root.trimmed().isEmpty())
    {
        return {};
    }
    return QDir(project_root).filePath(QStringLiteral("AUTOCOMPLETE.md"));
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

QString read_autocomplete_prompt(const QString &project_root, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    const QString prompt_path = autocomplete_prompt_file_path(project_root);
    if (prompt_path.isEmpty())
    {
        return {};
    }

    QFile file(prompt_path);
    if (file.exists() == false)
    {
        return {};
    }
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
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
                                           const prompt_instruction_options_t &options,
                                           QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    QStringList instructions;

    QString ignore_error;
    const bool ignore_global_prompt =
        project_ignores_global_system_prompt(project_root, &ignore_error);
    const QString normalized_global_prompt = normalized_instruction_text(options.global_prompt);
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

    QString standard_files_error;
    const QStringList standard_file_instructions =
        read_standard_instruction_files(project_root, options, &standard_files_error);
    instructions.append(standard_file_instructions);

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
        else if (standard_files_error.isEmpty() == false)
        {
            *error = standard_files_error;
        }
    }

    return instructions;
}

void append_configured_system_instructions(QList<chat_message_t> *messages,
                                           const QString &project_root,
                                           const prompt_instruction_options_t &options,
                                           QString *error)
{
    if (messages == nullptr)
    {
        return;
    }

    const QStringList instructions = configured_system_instructions(project_root, options, error);
    for (const QString &instruction : instructions)
    {
        messages->append(chat_message_t{QStringLiteral("system"), instruction});
    }
}

void append_autocomplete_system_instruction(QList<chat_message_t> *messages,
                                            const QString &project_root, QString *error)
{
    if (messages == nullptr)
    {
        return;
    }

    const QString instruction = read_autocomplete_prompt(project_root, error);
    if (instruction.isEmpty() == false)
    {
        messages->append(chat_message_t{QStringLiteral("system"), instruction});
    }
}

}  // namespace qcai2
