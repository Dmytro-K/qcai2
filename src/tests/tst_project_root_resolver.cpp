/*! @file
    @brief Regression tests for project-root resolution helpers.
*/

#include "../src/util/project_root_resolver.h"

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

namespace qcai2
{

class tst_project_root_resolver_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Standalone files must not treat their containing directory as a workspace root.
     */
    void project_root_for_file_path_returns_empty_without_project();
};

void tst_project_root_resolver_t::project_root_for_file_path_returns_empty_without_project()
{
    QTemporaryDir temporary_dir;
    QVERIFY(temporary_dir.isValid());

    const QString file_path = temporary_dir.filePath(QStringLiteral("standalone.cpp"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("int main() { return 0; }\n") > 0);
    file.close();

    QCOMPARE(project_root_for_file_path(file_path), QString());
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_project_root_resolver_t)

#include "tst_project_root_resolver.moc"
