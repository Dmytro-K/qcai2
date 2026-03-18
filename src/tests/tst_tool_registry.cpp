/*! Unit tests for the agent tool registry. */

#include <QtTest>

#include <QJsonArray>

#include "src/tools/i_tool.h"
#include "src/tools/tool_registry.h"

#include <memory>

using namespace qcai2;

namespace
{

class fake_tool_t final : public i_tool_t
{
public:
    fake_tool_t(QString tool_name, QString tool_description, QJsonObject schema = {})
        : tool_name(std::move(tool_name)), tool_description(std::move(tool_description)),
          schema(std::move(schema))
    {
    }

    QString name() const override
    {
        return this->tool_name;
    }

    QString description() const override
    {
        return this->tool_description;
    }

    QJsonObject args_schema() const override
    {
        return this->schema;
    }

    QString execute(const QJsonObject &, const QString &) override
    {
        return QStringLiteral("ok");
    }

private:
    QString tool_name;
    QString tool_description;
    QJsonObject schema;
};

}  // namespace

class tool_registry_test_t : public QObject
{
    Q_OBJECT

private slots:
    void register_tool_makes_lookup_and_sorted_listing_available();
    void register_tool_same_name_replaces_existing_tool();
    void unregister_tool_reports_whether_removal_happened();
    void tool_descriptions_json_serializes_registered_metadata();
};

void tool_registry_test_t::register_tool_makes_lookup_and_sorted_listing_available()
{
    tool_registry_t registry;
    const auto beta =
        std::make_shared<fake_tool_t>(QStringLiteral("beta"), QStringLiteral("Second tool"));
    const auto alpha =
        std::make_shared<fake_tool_t>(QStringLiteral("alpha"), QStringLiteral("First tool"));

    registry.register_tool(beta);
    registry.register_tool(alpha);

    QCOMPARE(registry.tool(QStringLiteral("alpha")), alpha.get());
    QCOMPARE(registry.tool(QStringLiteral("beta")), beta.get());
    QVERIFY(registry.tool(QStringLiteral("missing")) == nullptr);

    const QList<std::shared_ptr<i_tool_t>> tools = registry.all_tools();
    QCOMPARE(tools.size(), 2);
    QCOMPARE(tools.at(0)->name(), QStringLiteral("alpha"));
    QCOMPARE(tools.at(1)->name(), QStringLiteral("beta"));
}

void tool_registry_test_t::register_tool_same_name_replaces_existing_tool()
{
    tool_registry_t registry;
    const auto first =
        std::make_shared<fake_tool_t>(QStringLiteral("search"), QStringLiteral("Old description"));
    const auto replacement = std::make_shared<fake_tool_t>(
        QStringLiteral("search"), QStringLiteral("New description"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});

    registry.register_tool(first);
    registry.register_tool(replacement);

    QCOMPARE(registry.all_tools().size(), 1);
    QCOMPARE(registry.tool(QStringLiteral("search")), replacement.get());
    QCOMPARE(registry.tool_descriptions_json()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("description"))
                 .toString(),
             QStringLiteral("New description"));
}

void tool_registry_test_t::unregister_tool_reports_whether_removal_happened()
{
    tool_registry_t registry;
    registry.register_tool(
        std::make_shared<fake_tool_t>(QStringLiteral("build"), QStringLiteral("Runs build")));

    QVERIFY(registry.unregister_tool(QStringLiteral("build")));
    QVERIFY(!registry.unregister_tool(QStringLiteral("build")));
    QVERIFY(registry.tool(QStringLiteral("build")) == nullptr);
}

void tool_registry_test_t::tool_descriptions_json_serializes_registered_metadata()
{
    tool_registry_t registry;
    registry.register_tool(std::make_shared<fake_tool_t>(
        QStringLiteral("read_file"), QStringLiteral("Reads a file"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}));

    const QJsonArray descriptions = registry.tool_descriptions_json();

    QCOMPARE(descriptions.size(), 1);
    const QJsonObject description = descriptions.at(0).toObject();
    QCOMPARE(description.value(QStringLiteral("name")).toString(), QStringLiteral("read_file"));
    QCOMPARE(description.value(QStringLiteral("description")).toString(),
             QStringLiteral("Reads a file"));
    QCOMPARE(description.value(QStringLiteral("args"))
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("object"));
    QCOMPARE(description.value(QStringLiteral("args"))
                 .toObject()
                 .value(QStringLiteral("required"))
                 .toArray(),
             QJsonArray{QStringLiteral("path")});
}

QTEST_APPLESS_MAIN(tool_registry_test_t)

#include "tst_tool_registry.moc"
