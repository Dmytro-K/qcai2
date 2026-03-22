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
    void omits_global_prompt_when_project_requests_it();
    void appends_system_messages_in_order();
};

void tst_prompt_instructions_t::includes_global_prompt_when_not_ignored()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    QString error;
    const QStringList instructions = configured_system_instructions(
        temp_dir.path(), QStringLiteral("  Global prompt  "), &error);

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
        configured_system_instructions(temp_dir.path(), QStringLiteral("Global prompt"), &error);
    const QStringList expected_instructions = {QStringLiteral("Global prompt"),
                                               QStringLiteral("Project rules here")};

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
        configured_system_instructions(temp_dir.path(), QStringLiteral("Global prompt"), &error);

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
                                          QStringLiteral("Global prompt"), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(messages.size(), 3);
    QCOMPARE(messages.at(0).role, QStringLiteral("user"));
    QCOMPARE(messages.at(0).content, QStringLiteral("goal"));
    QCOMPARE(messages.at(1).role, QStringLiteral("system"));
    QCOMPARE(messages.at(1).content, QStringLiteral("Global prompt"));
    QCOMPARE(messages.at(2).role, QStringLiteral("system"));
    QCOMPARE(messages.at(2).content, QStringLiteral("Project rules"));
}

QTEST_MAIN(tst_prompt_instructions_t)

#include "tst_prompt_instructions.moc"
