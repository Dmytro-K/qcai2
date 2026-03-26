/*! Helpers for loading stable global and project-local system instructions. */
#pragma once

#include "../models/agent_messages.h"

#include <QString>
#include <QStringList>

namespace qcai2
{

struct settings_t;

struct prompt_instruction_options_t
{
    QString global_prompt;
    bool load_agents_md = true;
    bool load_github_copilot_instructions = true;
    bool load_claude_md = true;
    bool load_gemini_md = true;
    bool load_github_instructions_dir = true;
};

prompt_instruction_options_t prompt_instruction_options(const settings_t &settings);
QString project_session_state_file_path(const QString &project_root);
QString project_rules_file_path(const QString &project_root);
QString autocomplete_prompt_file_path(const QString &project_root);
QString read_project_rules(const QString &project_root, QString *error = nullptr);
QString read_autocomplete_prompt(const QString &project_root, QString *error = nullptr);
QStringList configured_system_instructions(const QString &project_root,
                                           const prompt_instruction_options_t &options,
                                           QString *error = nullptr);
void append_configured_system_instructions(QList<chat_message_t> *messages,
                                           const QString &project_root,
                                           const prompt_instruction_options_t &options,
                                           QString *error = nullptr);
void append_autocomplete_system_instruction(QList<chat_message_t> *messages,
                                            const QString &project_root, QString *error = nullptr);

}  // namespace qcai2
