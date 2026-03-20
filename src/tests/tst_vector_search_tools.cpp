#include "../src/tools/vector_search_tools.h"
#include "../src/vector_search/vector_search_backend.h"
#include "../src/vector_search/vector_search_service.h"

#include <QtTest>

using namespace qcai2;

namespace
{

class fake_vector_backend_t final : public vector_search_backend_t
{
public:
    QString backend_id() const override
    {
        return QStringLiteral("fake");
    }

    QString display_name() const override
    {
        return QStringLiteral("Fake");
    }

    bool is_configured() const override
    {
        return true;
    }

    QString configuration_summary() const override
    {
        return QStringLiteral("fake");
    }

    bool check_connection(QString *error = nullptr) const override
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    bool ensure_collection(int vector_dimensions, QString *error = nullptr) const override
    {
        Q_UNUSED(vector_dimensions);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    bool upsert_points(const QJsonArray &points, int vector_dimensions,
                       QString *error = nullptr) const override
    {
        Q_UNUSED(points);
        Q_UNUSED(vector_dimensions);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    bool delete_points(const QJsonObject &filter, QString *error = nullptr) const override
    {
        Q_UNUSED(filter);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    QList<vector_search_match_t> search(const QList<float> &query_vector,
                                        const QJsonObject &filter, int limit,
                                        QString *error = nullptr) const override
    {
        Q_UNUSED(query_vector);
        Q_UNUSED(limit);
        if (error != nullptr)
        {
            error->clear();
        }

        const QString entity_kind = filter.value(QStringLiteral("entity_kind")).toString();
        if (entity_kind == QStringLiteral("file"))
        {
            vector_search_match_t match;
            match.score = 0.91;
            match.line_start = 12;
            match.line_end = 18;
            match.file_path = QStringLiteral("/tmp/project/src/example.cpp");
            match.content = QStringLiteral("void example() { semantic retrieval(); }");
            match.payload =
                QJsonObject{{QStringLiteral("relative_path"), QStringLiteral("src/example.cpp")}};
            return {match};
        }

        vector_search_match_t match;
        match.score = 0.88;
        match.content = QStringLiteral("We already discussed qdrant indexing here.");
        match.payload =
            QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("conversation_id"), QStringLiteral("conversation-123")}};
        return {match};
    }
};

}  // namespace

class tst_vector_search_tools_t : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        auto &service = vector_search_service();
        service.set_enabled(false);
        service.set_backend(std::unique_ptr<vector_search_backend_t>{});
        QString error;
        QVERIFY(!service.verify_connection(&error));
    }

    void vector_search_schema_requires_query()
    {
        vector_search_tool_t tool;
        const QJsonObject schema = tool.args_schema();
        QCOMPARE(schema.value(QStringLiteral("query"))
                     .toObject()
                     .value(QStringLiteral("required"))
                     .toBool(),
                 true);
    }

    void vector_search_formats_file_matches()
    {
        auto &service = vector_search_service();
        service.set_enabled(true);
        service.set_backend(std::make_unique<fake_vector_backend_t>());
        QString error;
        QVERIFY(service.verify_connection(&error));

        vector_search_tool_t tool;
        const QString result = tool.execute(
            QJsonObject{{QStringLiteral("query"), QStringLiteral("semantic retrieval")}},
            QStringLiteral("/tmp/project"));
        QVERIFY(result.contains(QStringLiteral("Semantic file matches:")));
        QVERIFY(result.contains(QStringLiteral("src/example.cpp:12-18")));
    }

    void vector_search_history_formats_history_matches()
    {
        auto &service = vector_search_service();
        service.set_enabled(true);
        service.set_backend(std::make_unique<fake_vector_backend_t>());
        QString error;
        QVERIFY(service.verify_connection(&error));

        vector_search_history_tool_t tool;
        const QString result =
            tool.execute(QJsonObject{{QStringLiteral("query"), QStringLiteral("qdrant indexing")}},
                         QStringLiteral("/tmp/project"));
        QVERIFY(result.contains(QStringLiteral("Semantic history matches:")));
        QVERIFY(result.contains(QStringLiteral("conversation-123")));
    }
};

QTEST_GUILESS_MAIN(tst_vector_search_tools_t)

#include "tst_vector_search_tools.moc"
