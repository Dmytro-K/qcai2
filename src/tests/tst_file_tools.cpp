#include "../src/tools/file_tools.h"
#include "../src/util/diff.h"
#include "../src/util/logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdio>

namespace qcai2
{

namespace
{

bool file_tools_debug_enabled()
{
    const QString value = qEnvironmentVariable("QCAI2_FILE_TOOLS_DEBUG").trimmed();
    return value.isEmpty() == false && value != QStringLiteral("0") &&
           value.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0;
}

void emit_file_tools_debug(const QString &message)
{
    if (file_tools_debug_enabled() == false)
    {
        return;
    }

    std::fprintf(stderr, "[file_tools_debug] %s\n", qPrintable(message));
    std::fflush(stderr);
}

QString escaped_bytes(const QByteArray &bytes)
{
    QString escaped = QString::fromUtf8(bytes);
    escaped.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
    escaped.replace(QStringLiteral("\n"), QStringLiteral("\\n\n"));
    return escaped;
}

void log_file_state(const QString &label, const QString &path)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) == false)
    {
        emit_file_tools_debug(
            QStringLiteral("%1: failed to open %2: %3").arg(label, path, file.errorString()));
        return;
    }

    const QByteArray bytes = file.readAll();
    emit_file_tools_debug(QStringLiteral("%1: %2 (%3 bytes)\n%4")
                              .arg(label, path)
                              .arg(bytes.size())
                              .arg(escaped_bytes(bytes)));
}

void log_file_tools_environment_once()
{
    static bool logged = false;
    if (logged == true)
    {
        return;
    }
    logged = true;

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    emit_file_tools_debug(QStringLiteral("platform=%1 current_dir=%2")
                              .arg(QSysInfo::prettyProductName(), QDir::currentPath()));
    emit_file_tools_debug(QStringLiteral("PATH=%1").arg(env.value(QStringLiteral("PATH"))));
    emit_file_tools_debug(QStringLiteral("git_executable=%1")
                              .arg(QStandardPaths::findExecutable(QStringLiteral("git"))));
}

void write_text_file(const QString &path, const QString &content)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content.toUtf8());
    file.close();
}

QString read_text_file(const QString &path)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) == false)
    {
        qFatal("Failed to open file for reading: %s", qPrintable(path));
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

class tst_file_tools_t : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void apply_patch_tool_updates_existing_file();
    void apply_patch_tool_creates_new_file_and_updates_existing_file();
    void apply_patch_tool_rejects_new_file_outside_project();
    void diff_apply_patch_dry_run_keeps_file_unchanged();
    void diff_apply_patch_reports_empty_work_dir();
};

void tst_file_tools_t::initTestCase()
{
    if (file_tools_debug_enabled() == true)
    {
        QObject::connect(&logger_t::instance(), &logger_t::entry_added, &logger_t::instance(),
                         [](const QString &entry) { emit_file_tools_debug(entry); });
    }

    log_file_tools_environment_once();
}

void tst_file_tools_t::init()
{
    logger_t::instance().clear();
    logger_t::instance().set_enabled(file_tools_debug_enabled());
    emit_file_tools_debug(QStringLiteral("=== starting test: %1 ===")
                              .arg(QString::fromLatin1(QTest::currentTestFunction())));
}

void tst_file_tools_t::apply_patch_tool_updates_existing_file()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString source_path = QDir(temp_dir.path()).filePath(QStringLiteral("src/example.c"));
    write_text_file(source_path, QStringLiteral("int value = 1;\n"));
    emit_file_tools_debug(QStringLiteral("temp_dir=%1").arg(temp_dir.path()));
    log_file_state(QStringLiteral("source before apply_patch_tool_updates_existing_file"),
                   source_path);

    apply_patch_tool_t tool;
    const QString diff = QStringLiteral("--- a/src/example.c\n"
                                        "+++ b/src/example.c\n"
                                        "@@ -1 +1 @@\n"
                                        "-int value = 1;\n"
                                        "+int value = 2;\n");
    emit_file_tools_debug(QStringLiteral("diff:\n%1").arg(diff));

    const QString result =
        tool.execute(QJsonObject{{QStringLiteral("diff"), diff}}, temp_dir.path());
    emit_file_tools_debug(QStringLiteral("tool result: %1").arg(result));
    log_file_state(QStringLiteral("source after apply_patch_tool_updates_existing_file"),
                   source_path);

    QVERIFY2(result.contains(QStringLiteral("Patch applied successfully.")), qPrintable(result));
    QCOMPARE(read_text_file(source_path), QStringLiteral("int value = 2;\n"));
}

void tst_file_tools_t::apply_patch_tool_creates_new_file_and_updates_existing_file()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString source_path = QDir(temp_dir.path()).filePath(QStringLiteral("src/example.c"));
    const QString header_path =
        QDir(temp_dir.path()).filePath(QStringLiteral("include/example.h"));
    write_text_file(source_path, QStringLiteral("int value = 1;\n"));
    emit_file_tools_debug(QStringLiteral("temp_dir=%1").arg(temp_dir.path()));
    log_file_state(QStringLiteral("source before new file patch"), source_path);

    apply_patch_tool_t tool;
    const QString diff = QStringLiteral("=== NEW FILE: include/example.h ===\n"
                                        "#pragma once\n"
                                        "\n"
                                        "int example(void);\n"
                                        "\n"
                                        "=== MODIFIED: src/example.c ===\n"
                                        "--- a/src/example.c\n"
                                        "+++ b/src/example.c\n"
                                        "@@ -1 +1 @@\n"
                                        "-int value = 1;\n"
                                        "+int example(void) { return 2; }\n");
    emit_file_tools_debug(QStringLiteral("diff:\n%1").arg(diff));

    const QString result =
        tool.execute(QJsonObject{{QStringLiteral("diff"), diff}}, temp_dir.path());
    emit_file_tools_debug(QStringLiteral("tool result: %1").arg(result));
    log_file_state(QStringLiteral("source after new file patch"), source_path);
    log_file_state(QStringLiteral("header after new file patch"), header_path);

    QVERIFY2(result.contains(QStringLiteral("Patch applied successfully.")), qPrintable(result));
    QVERIFY(result.contains(QStringLiteral("1 new file(s) created: include/example.h")));
    QCOMPARE(read_text_file(source_path), QStringLiteral("int example(void) { return 2; }\n"));
    QCOMPARE(read_text_file(header_path), QStringLiteral("#pragma once\n\nint example(void);\n"));
}

void tst_file_tools_t::apply_patch_tool_rejects_new_file_outside_project()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString escaped_path =
        QDir(temp_dir.path()).filePath(QStringLiteral("../include/example.h"));

    apply_patch_tool_t tool;
    const QString diff = QStringLiteral("=== NEW FILE: ../include/example.h ===\n"
                                        "#pragma once\n");
    emit_file_tools_debug(QStringLiteral("temp_dir=%1").arg(temp_dir.path()));
    emit_file_tools_debug(QStringLiteral("diff:\n%1").arg(diff));

    const QString result =
        tool.execute(QJsonObject{{QStringLiteral("diff"), diff}}, temp_dir.path());
    emit_file_tools_debug(QStringLiteral("tool result: %1").arg(result));
    log_file_state(QStringLiteral("header after rejected new file patch"), escaped_path);

    QVERIFY(result.contains(QStringLiteral("outside project")));
    QVERIFY(QFileInfo::exists(escaped_path) == false);
}

void tst_file_tools_t::diff_apply_patch_dry_run_keeps_file_unchanged()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString source_path = QDir(temp_dir.path()).filePath(QStringLiteral("src/example.c"));
    write_text_file(source_path, QStringLiteral("int value = 1;\n"));
    emit_file_tools_debug(QStringLiteral("temp_dir=%1").arg(temp_dir.path()));
    log_file_state(QStringLiteral("source before dry run"), source_path);

    const QString diff = QStringLiteral("--- a/src/example.c\n"
                                        "+++ b/src/example.c\n"
                                        "@@ -1 +1 @@\n"
                                        "-int value = 1;\n"
                                        "+int value = 2;\n");
    emit_file_tools_debug(QStringLiteral("diff:\n%1").arg(diff));

    QString error;
    const bool ok = Diff::apply_patch(diff, temp_dir.path(), true, error);
    emit_file_tools_debug(QStringLiteral("dry run ok=%1 error=%2")
                              .arg(ok ? QStringLiteral("true") : QStringLiteral("false"), error));
    log_file_state(QStringLiteral("source after dry run"), source_path);
    QVERIFY(ok);
    QVERIFY(error.isEmpty());
    QCOMPARE(read_text_file(source_path), QStringLiteral("int value = 1;\n"));
}

void tst_file_tools_t::diff_apply_patch_reports_empty_work_dir()
{
    const QString diff = QStringLiteral("--- a/src/example.c\n"
                                        "+++ b/src/example.c\n"
                                        "@@ -1 +1 @@\n"
                                        "-int value = 1;\n"
                                        "+int value = 2;\n");

    QString error;
    const bool ok = Diff::apply_patch(diff, QString(), true, error);
    emit_file_tools_debug(QStringLiteral("empty work dir ok=%1 error=%2")
                              .arg(ok ? QStringLiteral("true") : QStringLiteral("false"), error));
    QVERIFY(ok == false);
    QVERIFY(error.contains(QStringLiteral("working directory is empty"), Qt::CaseInsensitive));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_file_tools_t)

#include "tst_file_tools.moc"
