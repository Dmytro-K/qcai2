/*! @file
    @brief Implements path-based accessors for JSON objects and arrays.
*/

#include "Json.h"

namespace qcai2::Json
{

/**
 * @brief Traverses nested object keys and array indexes from a root object.
 * @param root Root JSON object used as the traversal starting point.
 * @param parts Ordered path segments split from a slash-delimited path.
 * @return Resolved JSON value, or an undefined value when traversal fails.
 */
static QJsonValue traverse(const QJsonObject &root, const QStringList &parts)
{
    QJsonValue cur = root;
    for (const QString &part : parts)
    {
        if (cur.isObject())
        {
            cur = cur.toObject().value(part);
        }
        else if (cur.isArray())
        {
            bool ok;
            int idx = part.toInt(&ok);
            if (!ok)
            {
                return {};
            }
            const QJsonArray arr = cur.toArray();
            if (idx < 0 || idx >= arr.size())
            {
                return {};
            }
            cur = arr.at(idx);
        }
        else
        {
            return {};
        }
    }
    return cur;
}

QString getString(const QJsonObject &root, const QString &path, const QString &def)
{
    QJsonValue v = traverse(root, path.split('/'));
    return v.isString() ? v.toString() : def;
}

int getInt(const QJsonObject &root, const QString &path, int def)
{
    QJsonValue v = traverse(root, path.split('/'));
    return v.isDouble() ? static_cast<int>(v.toDouble()) : def;
}

QJsonObject errorObject(const QString &message)
{
    return QJsonObject{{"error", message}};
}

}  // namespace qcai2::Json
