/*! @file
    @brief Unit tests for JSON path helpers used by provider adapters.
*/

#include <QtTest>

#include "src/util/Json.h"

using namespace qcai2;

/**
 * @brief Exercises nested-path access, defaults, and array filtering helpers.
 */
class JsonTest : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Reads a string through nested object keys.
     */
    void getString_readsNestedObjectPath();

    /**
     * @brief Reads a string through an object key followed by an array index.
     */
    void getString_readsArrayPath();

    /**
     * @brief Falls back when a path is missing, mistyped, or not indexable.
     */
    void getString_returnsDefaultForMissingOrWrongType();

    /**
     * @brief Converts numeric JSON values to integers.
     */
    void getInt_readsNumericValue();

    /**
     * @brief Returns the provided default for missing or non-numeric values.
     */
    void getInt_returnsDefaultForMissingValue();

    /**
     * @brief Builds the expected one-field error payload.
     */
    void errorObject_buildsExpectedPayload();

    /**
     * @brief Visits only object entries when arrays contain mixed value types.
     */
    void forEachObject_skipsNonObjects();

};

void JsonTest::getString_readsNestedObjectPath()
{
    const QJsonObject root{
        {"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", "hello"}}}}}}};

    QCOMPARE(Json::getString(root, "choices/0/message/content"), QString("hello"));
}

void JsonTest::getString_readsArrayPath()
{
    const QJsonObject root{
        {"items", QJsonArray{QJsonObject{{"name", "first"}}, QJsonObject{{"name", "second"}}}}};

    QCOMPARE(Json::getString(root, "items/1/name"), QString("second"));
}

void JsonTest::getString_returnsDefaultForMissingOrWrongType()
{
    const QJsonObject root{{"answer", 42}, {"items", QJsonArray{QJsonObject{{"name", "first"}}}}};

    QCOMPARE(Json::getString(root, "missing/path", "fallback"), QString("fallback"));
    QCOMPARE(Json::getString(root, "answer", "fallback"), QString("fallback"));
    QCOMPARE(Json::getString(root, "items/not-an-index/name", "fallback"), QString("fallback"));
}

void JsonTest::getInt_readsNumericValue()
{
    const QJsonObject root{{"usage", QJsonObject{{"total_tokens", 42.9}}}};

    QCOMPARE(Json::getInt(root, "usage/total_tokens"), 42);
}

void JsonTest::getInt_returnsDefaultForMissingValue()
{
    const QJsonObject root{{"usage", QJsonObject{{"label", "many"}}}};

    QCOMPARE(Json::getInt(root, "usage/prompt_tokens", 7), 7);
    QCOMPARE(Json::getInt(root, "usage/label", 9), 9);
}

void JsonTest::errorObject_buildsExpectedPayload()
{
    const QJsonObject error = Json::errorObject("boom");

    QCOMPARE(error.value("error").toString(), QString("boom"));
    QCOMPARE(error.size(), 1);
}

void JsonTest::forEachObject_skipsNonObjects()
{
    const QJsonArray values{QJsonObject{{"name", "first"}}, 5, QString("ignored"),
                            QJsonObject{{"name", "second"}}};

    QStringList names;
    Json::forEachObject(
        values, [&names](const QJsonObject &obj) { names.append(obj.value("name").toString()); });

    QCOMPARE(names, QStringList({"first", "second"}));
}

QTEST_APPLESS_MAIN(JsonTest)

#include "tst_json.moc"
