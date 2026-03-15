#pragma once

/*! @file
    @brief Small helpers for reading structured JSON responses.
*/

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace qcai2::Json
{

/**
 * @brief Reads a string value from a slash-delimited JSON path.
 * @param root Root JSON object to traverse.
 * @param path Nested path such as @c "choices/0/message/content".
 * @param def Fallback returned when the path is missing or not a string.
 * @return Resolved string value or @p def when traversal fails.
 */
QString getString(const QJsonObject &root, const QString &path, const QString &def = {});

/**
 * @brief Reads an integer-like value from a slash-delimited JSON path.
 * @param root Root JSON object to traverse.
 * @param path Nested path using object keys and array indexes.
 * @param def Fallback returned when the path is missing or not numeric.
 * @return Integer conversion of the resolved value or @p def.
 */
int getInt(const QJsonObject &root, const QString &path, int def = 0);

/**
 * @brief Builds a one-field JSON object that carries an error string.
 * @param message Human-readable error description.
 * @return JSON object containing an @c error property.
 */
QJsonObject errorObject(const QString &message);

/**
 * @brief Invokes a callable for each object element in an array.
 * @tparam Fn Callable type that accepts a @c QJsonObject.
 * @param arr Array to scan.
 * @param fn Callable invoked only for entries whose value is an object.
 */
template <typename Fn> void forEachObject(const QJsonArray &arr, Fn &&fn)
{
    for (const auto &v : arr)
    {
        {
            if (v.isObject() == true)
            {
                {
                    fn(v.toObject());
                }
            }
        }
    }
}

}  // namespace qcai2::Json
