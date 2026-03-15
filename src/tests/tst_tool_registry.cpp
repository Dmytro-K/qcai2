/*! Unit tests for the agent tool registry. */

#include <QtTest>

#include <QJsonArray>

#include "src/tools/ITool.h"
#include "src/tools/ToolRegistry.h"

#include <memory>

using namespace qcai2;

namespace
{

class FakeTool final : public ITool
{
public:
    FakeTool(QString toolName, QString toolDescription, QJsonObject schema = {})
        : m_name(std::move(toolName)),
          m_description(std::move(toolDescription)),
          m_schema(std::move(schema))
    {
    }

    QString name() const override
    {
        return m_name;
    }

    QString description() const override
    {
        return m_description;
    }

    QJsonObject argsSchema() const override
    {
        return m_schema;
    }

    QString execute(const QJsonObject &, const QString &) override
    {
        return QStringLiteral("ok");
    }

private:
    QString m_name;
    QString m_description;
    QJsonObject m_schema;
};

}  // namespace

class ToolRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void registerTool_makesLookupAndSortedListingAvailable();
    void registerTool_sameNameReplacesExistingTool();
    void unregisterTool_reportsWhetherRemovalHappened();
    void toolDescriptionsJson_serializesRegisteredMetadata();
};

void ToolRegistryTest::registerTool_makesLookupAndSortedListingAvailable()
{
    ToolRegistry registry;
    const auto beta = std::make_shared<FakeTool>(QStringLiteral("beta"),
                                                 QStringLiteral("Second tool"));
    const auto alpha = std::make_shared<FakeTool>(QStringLiteral("alpha"),
                                                  QStringLiteral("First tool"));

    registry.registerTool(beta);
    registry.registerTool(alpha);

    QCOMPARE(registry.tool(QStringLiteral("alpha")), alpha.get());
    QCOMPARE(registry.tool(QStringLiteral("beta")), beta.get());
    QVERIFY(registry.tool(QStringLiteral("missing")) == nullptr);

    const QList<std::shared_ptr<ITool>> tools = registry.allTools();
    QCOMPARE(tools.size(), 2);
    QCOMPARE(tools.at(0)->name(), QStringLiteral("alpha"));
    QCOMPARE(tools.at(1)->name(), QStringLiteral("beta"));
}

void ToolRegistryTest::registerTool_sameNameReplacesExistingTool()
{
    ToolRegistry registry;
    const auto first = std::make_shared<FakeTool>(QStringLiteral("search"),
                                                  QStringLiteral("Old description"));
    const auto replacement = std::make_shared<FakeTool>(
        QStringLiteral("search"), QStringLiteral("New description"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});

    registry.registerTool(first);
    registry.registerTool(replacement);

    QCOMPARE(registry.allTools().size(), 1);
    QCOMPARE(registry.tool(QStringLiteral("search")), replacement.get());
    QCOMPARE(registry.toolDescriptionsJson().at(0).toObject().value(QStringLiteral("description")).toString(),
             QStringLiteral("New description"));
}

void ToolRegistryTest::unregisterTool_reportsWhetherRemovalHappened()
{
    ToolRegistry registry;
    registry.registerTool(
        std::make_shared<FakeTool>(QStringLiteral("build"), QStringLiteral("Runs build")));

    QVERIFY(registry.unregisterTool(QStringLiteral("build")));
    QVERIFY(!registry.unregisterTool(QStringLiteral("build")));
    QVERIFY(registry.tool(QStringLiteral("build")) == nullptr);
}

void ToolRegistryTest::toolDescriptionsJson_serializesRegisteredMetadata()
{
    ToolRegistry registry;
    registry.registerTool(std::make_shared<FakeTool>(
        QStringLiteral("read_file"), QStringLiteral("Reads a file"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}));

    const QJsonArray descriptions = registry.toolDescriptionsJson();

    QCOMPARE(descriptions.size(), 1);
    const QJsonObject description = descriptions.at(0).toObject();
    QCOMPARE(description.value(QStringLiteral("name")).toString(), QStringLiteral("read_file"));
    QCOMPARE(description.value(QStringLiteral("description")).toString(),
             QStringLiteral("Reads a file"));
    QCOMPARE(description.value(QStringLiteral("args")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("object"));
    QCOMPARE(description.value(QStringLiteral("args")).toObject().value(QStringLiteral("required")).toArray(),
             QJsonArray{QStringLiteral("path")});
}

QTEST_APPLESS_MAIN(ToolRegistryTest)

#include "tst_tool_registry.moc"
