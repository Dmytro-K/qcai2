#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace qcai2::Json
{

// Get a string from a nested path, e.g. path="choices/0/message/content"
QString getString(const QJsonObject &root, const QString &path, const QString &def = {});

// Get int
int getInt(const QJsonObject &root, const QString &path, int def = 0);

// Build a simple error response object
QJsonObject errorObject(const QString &message);

// Safe array iteration helper
template <typename Fn> void forEachObject(const QJsonArray &arr, Fn &&fn)
{
    for (const QJsonValue &v : arr)
        if (v.isObject())
            fn(v.toObject());
}

}  // namespace qcai2::Json
