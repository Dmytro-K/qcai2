/*! @file
    @brief Unit tests for JSON path helpers used by provider adapters.
*/

#include <QtTest>

#include "src/util/Json.h"

using namespace qcai2;

/**
 * @brief Exercises nested-path access, defaults, and array filtering helpers.
 */
class json_test_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Reads a string through nested object keys.
     */
    void get_string_reads_nested_object_path();

    /**
     * @brief Reads a string through an object key followed by an array index.
     */
    void get_string_reads_array_path();

    /**
     * @brief Falls back when a path is missing, mistyped, or not indexable.
     */
    void get_string_returns_default_for_missing_or_wrong_type();

    /**
     * @brief Converts numeric JSON values to integers.
     */
    void get_int_reads_numeric_value();

    /**
     * @brief Returns the provided default for missing or non-numeric values.
     */
    void get_int_returns_default_for_missing_value();

    /**
     * @brief Builds the expected one-field error payload.
     */
    void error_object_builds_expected_payload();

    /**
     * @brief Visits only object entries when arrays contain mixed value types.
     */
    void for_each_object_skips_non_objects();
};

void json_test_t::get_string_reads_nested_object_path()
{
    const QJsonObject root{
        {"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", "hello"}}}}}}};

    QCOMPARE(json::get_string(root, "choices/0/message/content"), QString("hello"));
}

void json_test_t::get_string_reads_array_path()
{
    const QJsonObject root{
        {"items", QJsonArray{QJsonObject{{"name", "first"}}, QJsonObject{{"name", "second"}}}}};

    QCOMPARE(json::get_string(root, "items/1/name"), QString("second"));
}

void json_test_t::get_string_returns_default_for_missing_or_wrong_type()
{
    const QJsonObject root{{"answer", 42}, {"items", QJsonArray{QJsonObject{{"name", "first"}}}}};

    QCOMPARE(json::get_string(root, "missing/path", "fallback"), QString("fallback"));
    QCOMPARE(json::get_string(root, "answer", "fallback"), QString("fallback"));
    QCOMPARE(json::get_string(root, "items/not-an-index/name", "fallback"), QString("fallback"));
}

void json_test_t::get_int_reads_numeric_value()
{
    const QJsonObject root{{"usage", QJsonObject{{"total_tokens", 42.9}}}};

    QCOMPARE(json::get_int(root, "usage/total_tokens"), 42);
}

void json_test_t::get_int_returns_default_for_missing_value()
{
    const QJsonObject root{{"usage", QJsonObject{{"label", "many"}}}};

    QCOMPARE(json::get_int(root, "usage/prompt_tokens", 7), 7);
    QCOMPARE(json::get_int(root, "usage/label", 9), 9);
}

void json_test_t::error_object_builds_expected_payload()
{
    const QJsonObject error = json::error_object("boom");

    QCOMPARE(error.value("error").toString(), QString("boom"));
    QCOMPARE(error.size(), 1);
}

void json_test_t::for_each_object_skips_non_objects()
{
    const QJsonArray values{QJsonObject{{"name", "first"}}, 5, QString("ignored"),
                            QJsonObject{{"name", "second"}}};

    QStringList names;
    json::for_each_object(
        values, [&names](const QJsonObject &obj) { names.append(obj.value("name").toString()); });

    QCOMPARE(names, QStringList({"first", "second"}));
}

QTEST_APPLESS_MAIN(json_test_t)

#include "tst_json.moc"
