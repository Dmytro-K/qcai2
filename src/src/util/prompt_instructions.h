/*! Helpers for loading stable global and project-local system instructions. */
#pragma once

#include "../models/agent_messages.h"

#include <QString>
#include <QStringList>

namespace qcai2
{

QString project_session_state_file_path(const QString &project_root);
QString project_rules_file_path(const QString &project_root);
QString read_project_rules(const QString &project_root, QString *error = nullptr);
QStringList configured_system_instructions(const QString &project_root,
                                           const QString &global_prompt, QString *error = nullptr);
void append_configured_system_instructions(QList<chat_message_t> *messages,
                                           const QString &project_root,
                                           const QString &global_prompt, QString *error = nullptr);

}  // namespace qcai2
