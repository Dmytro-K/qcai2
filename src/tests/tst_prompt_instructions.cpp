#include "../src/util/prompt_instructions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qcai2;

namespace
{

bool write_text_file(const QString &path, const QString &content)
{
    const QFileInfo file_info(path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        return false;
    }
    file.write(content.toUtf8());
    return file.commit();
}

bool write_json_file(const QString &path, const QJsonObject &root)
{
    const QFileInfo file_info(path);
    if (QDir().mkpath(file_info.absolutePath()) == false)
    {
        return false;
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
    {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}  // namespace

class tst_prompt_instructions_t : public QObject
{
    Q_OBJECT

private slots:
    void includes_global_prompt_when_not_ignored();
    void appends_project_rules_after_global_prompt();
    void appends_standard_ai_instruction_files_after_project_rules();
    void appends_recursive_github_instruction_files_in_sorted_order();
    void disabled_standard_ai_instruction_sources_are_skipped();
    void omits_global_prompt_when_project_requests_it();
    void appends_system_messages_in_order();
    void autocomplete_uses_only_autocomplete_md();
    void autocomplete_missing_file_is_non_fatal();
};

namespace
{

prompt_instruction_options_t default_instruction_options()
{
    return {.global_prompt = QStringLiteral("Global prompt")};
}

}  // namespace

void tst_prompt_instructions_t::includes_global_prompt_when_not_ignored()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QString error;
    prompt_instruction_options_t s = default_instruction_options();
    s.global_prompt = QStringLiteral("  Global prompt  ");
    const QStringList instructions = configured_system_instructions(temp_dir.path(), s, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(instructions, QStringList{QStringLiteral("Global prompt")});
}

void tst_prompt_instructions_t::appends_project_rules_after_global_prompt()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString rules_path = project_rules_file_path(temp_dir.path());
    QVERIFY(write_text_file(rules_path, QStringLiteral("\n  Project rules here  \n")));

    QString error;
    const QStringList instructions =
        configured_system_instructions(temp_dir.path(), default_instruction_options(), &error);
    const QStringList expected_instructions = {QStringLiteral("Global prompt"),
                                               QStringLiteral("Project rules here")};

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(instructions, expected_instructions);
}

void tst_prompt_instructions_t::appends_standard_ai_instruction_files_after_project_rules()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(project_rules_file_path(temp_dir.path()),
                            QStringLiteral("Project rules here")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("AGENTS.md")),
                            QStringLiteral("Agent instructions")));
    QVERIFY(write_text_file(
        QDir(temp_dir.path()).filePath(QStringLiteral(".github/copilot-instructions.md")),
        QStringLiteral("Copilot instructions")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("CLAUDE.md")),
                            QStringLiteral("Claude instructions")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("GEMINI.md")),
                            QStringLiteral("Gemini instructions")));

    QString error;
    const QStringList instructions =
        configured_system_instructions(temp_dir.path(), default_instruction_options(), &error);
    const QStringList expected_instructions = {
        QStringLiteral("Global prompt"),       QStringLiteral("Project rules here"),
        QStringLiteral("Agent instructions"),  QStringLiteral("Copilot instructions"),
        QStringLiteral("Claude instructions"), QStringLiteral("Gemini instructions")};

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(instructions, expected_instructions);
}

void tst_prompt_instructions_t::appends_recursive_github_instruction_files_in_sorted_order()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(
        QDir(temp_dir.path())
            .filePath(QStringLiteral(".github/instructions/backend/database.instructions.md")),
        QStringLiteral("Database instructions")));
    QVERIFY(write_text_file(
        QDir(temp_dir.path()).filePath(QStringLiteral(".github/instructions/ui.instructions.md")),
        QStringLiteral("UI instructions")));
    QVERIFY(write_text_file(
        QDir(temp_dir.path())
            .filePath(QStringLiteral(".github/instructions/backend/api.instructions.md")),
        QStringLiteral("API instructions")));

    QString error;
    const QStringList instructions =
        configured_system_instructions(temp_dir.path(), default_instruction_options(), &error);
    const QStringList expected_instructions = {
        QStringLiteral("Global prompt"), QStringLiteral("API instructions"),
        QStringLiteral("Database instructions"), QStringLiteral("UI instructions")};

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(instructions, expected_instructions);
}

void tst_prompt_instructions_t::disabled_standard_ai_instruction_sources_are_skipped()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("AGENTS.md")),
                            QStringLiteral("Agent instructions")));
    QVERIFY(write_text_file(
        QDir(temp_dir.path()).filePath(QStringLiteral(".github/copilot-instructions.md")),
        QStringLiteral("Copilot instructions")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("CLAUDE.md")),
                            QStringLiteral("Claude instructions")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("GEMINI.md")),
                            QStringLiteral("Gemini instructions")));
    QVERIFY(write_text_file(
        QDir(temp_dir.path()).filePath(QStringLiteral(".github/instructions/ui.instructions.md")),
        QStringLiteral("UI instructions")));

    prompt_instruction_options_t s = default_instruction_options();
    s.load_agents_md = false;
    s.load_claude_md = false;
    s.load_github_instructions_dir = false;

    QString error;
    const QStringList instructions = configured_system_instructions(temp_dir.path(), s, &error);
    const QStringList expected_instructions = {QStringLiteral("Global prompt"),
                                               QStringLiteral("Copilot instructions"),
                                               QStringLiteral("Gemini instructions")};

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(instructions, expected_instructions);
}

void tst_prompt_instructions_t::omits_global_prompt_when_project_requests_it()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(project_rules_file_path(temp_dir.path()),
                            QStringLiteral("Project-only instructions")));
    QVERIFY(write_json_file(project_session_state_file_path(temp_dir.path()),
                            QJsonObject{{QStringLiteral("ignoreGlobalSystemPrompt"), true}}));

    QString error;
    const QStringList instructions =
        configured_system_instructions(temp_dir.path(), default_instruction_options(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(instructions, QStringList{QStringLiteral("Project-only instructions")});
}

void tst_prompt_instructions_t::appends_system_messages_in_order()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(project_rules_file_path(temp_dir.path()),
                            QStringLiteral("Project rules")));

    QList<chat_message_t> messages{{QStringLiteral("user"), QStringLiteral("goal")}};
    QString error;
    append_configured_system_instructions(&messages, temp_dir.path(),
                                          default_instruction_options(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(messages.size(), 3);
    QCOMPARE(messages.at(0).role, QStringLiteral("user"));
    QCOMPARE(messages.at(0).content, QStringLiteral("goal"));
    QCOMPARE(messages.at(1).role, QStringLiteral("system"));
    QCOMPARE(messages.at(1).content, QStringLiteral("Global prompt"));
    QCOMPARE(messages.at(2).role, QStringLiteral("system"));
    QCOMPARE(messages.at(2).content, QStringLiteral("Project rules"));
}

void tst_prompt_instructions_t::autocomplete_uses_only_autocomplete_md()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(project_rules_file_path(temp_dir.path()),
                            QStringLiteral("Project rules")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("AGENTS.md")),
                            QStringLiteral("Agent instructions")));
    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("AUTOCOMPLETE.md")),
                            QStringLiteral("  Autocomplete-only instructions  ")));

    QList<chat_message_t> messages{{QStringLiteral("user"), QStringLiteral("goal")}};
    QString error;
    append_autocomplete_system_instruction(&messages, temp_dir.path(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).role, QStringLiteral("user"));
    QCOMPARE(messages.at(1).role, QStringLiteral("system"));
    QCOMPARE(messages.at(1).content, QStringLiteral("Autocomplete-only instructions"));
}

void tst_prompt_instructions_t::autocomplete_missing_file_is_non_fatal()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QVERIFY(write_text_file(QDir(temp_dir.path()).filePath(QStringLiteral("AGENTS.md")),
                            QStringLiteral("Agent instructions")));

    QList<chat_message_t> messages{{QStringLiteral("user"), QStringLiteral("goal")}};
    QString error;
    append_autocomplete_system_instruction(&messages, temp_dir.path(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.at(0).role, QStringLiteral("user"));
    QCOMPARE(messages.at(0).content, QStringLiteral("goal"));
    QCOMPARE(read_autocomplete_prompt(temp_dir.path(), &error), QString());
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

QTEST_MAIN(tst_prompt_instructions_t)

#include "tst_prompt_instructions.moc"
