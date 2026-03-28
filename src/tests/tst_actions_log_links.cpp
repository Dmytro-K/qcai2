#include "../src/ui/actions_log_links.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

namespace qcai2
{

class tst_actions_log_links_t : public QObject
{
    Q_OBJECT

private slots:
    void plain_file_reference_becomes_internal_link();
    void fenced_code_block_is_not_rewritten();
    void backticked_file_reference_becomes_internal_link();
    void parse_internal_link_extracts_location();
    void resolve_relative_link_stays_inside_project();
};

void tst_actions_log_links_t::plain_file_reference_becomes_internal_link()
{
    const QString rendered = linkify_actions_log_file_references(
        QStringLiteral("See src/src/settings/settings_page.cpp:918 for details."));

    const QRegularExpression link_re(QStringLiteral(R"(\((qcai2-open-file:[^)]+)\))"));
    const QRegularExpressionMatch match = link_re.match(rendered);
    QVERIFY(match.hasMatch());
    QVERIFY(rendered.contains(QStringLiteral("[src/src/settings/settings_page.cpp:918](")));

    const std::optional<actions_log_file_link_t> link =
        parse_actions_log_file_link(QUrl(match.captured(1)));
    QVERIFY(link.has_value());
    QCOMPARE(link->path, QStringLiteral("src/src/settings/settings_page.cpp"));
    QCOMPARE(link->line, 918);
    QCOMPARE(link->column, 0);
}

void tst_actions_log_links_t::fenced_code_block_is_not_rewritten()
{
    const QString markdown = QStringLiteral(
        "Before\n\n~~~~text\nsrc/src/settings/settings_page.cpp:918\n~~~~\n\nAfter");
    QCOMPARE(linkify_actions_log_file_references(markdown), markdown);
}

void tst_actions_log_links_t::backticked_file_reference_becomes_internal_link()
{
    const QString rendered = linkify_actions_log_file_references(
        QStringLiteral("See `src/src/settings/settings_page.cpp:918:7` for details."));

    QVERIFY(rendered.contains(QStringLiteral("[src/src/settings/settings_page.cpp:918:7](")));
    const QRegularExpression link_re(QStringLiteral(R"(\((qcai2-open-file:[^)]+)\))"));
    const QRegularExpressionMatch match = link_re.match(rendered);
    QVERIFY(match.hasMatch());

    const std::optional<actions_log_file_link_t> link =
        parse_actions_log_file_link(QUrl(match.captured(1)));
    QVERIFY(link.has_value());
    QCOMPARE(link->path, QStringLiteral("src/src/settings/settings_page.cpp"));
    QCOMPARE(link->line, 918);
    QCOMPARE(link->column, 7);
}

void tst_actions_log_links_t::parse_internal_link_extracts_location()
{
    const QUrl url(
        QStringLiteral("qcai2-open-file:/open?path=%2Ftmp%2Fmain.cpp&line=11&column=3"));
    const std::optional<actions_log_file_link_t> link = parse_actions_log_file_link(url);
    QVERIFY(link.has_value());
    QCOMPARE(link->path, QStringLiteral("/tmp/main.cpp"));
    QCOMPARE(link->line, 11);
    QCOMPARE(link->column, 3);
}

void tst_actions_log_links_t::resolve_relative_link_stays_inside_project()
{
    QTemporaryDir project_dir;
    QVERIFY(project_dir.isValid());

    QDir dir(project_dir.path());
    QVERIFY(dir.mkpath(QStringLiteral("src")));

    QFile inside_file(dir.filePath(QStringLiteral("src/main.cpp")));
    QVERIFY(inside_file.open(QIODevice::WriteOnly));
    inside_file.write("int main() {}\n");
    inside_file.close();

    QFile outside_file(dir.filePath(QStringLiteral("../outside.cpp")));
    QVERIFY(outside_file.open(QIODevice::WriteOnly));
    outside_file.write("int outside() {}\n");
    outside_file.close();

    const QString inside_path = resolve_actions_log_file_link_path(
        actions_log_file_link_t{QStringLiteral("src/main.cpp"), 1, 0}, project_dir.path());
    QVERIFY(inside_path.endsWith(QStringLiteral("/src/main.cpp")));

    const QString escaped_path = resolve_actions_log_file_link_path(
        actions_log_file_link_t{QStringLiteral("../outside.cpp"), 1, 0}, project_dir.path());
    QVERIFY(escaped_path.isEmpty());
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_actions_log_links_t)

#include "tst_actions_log_links.moc"
