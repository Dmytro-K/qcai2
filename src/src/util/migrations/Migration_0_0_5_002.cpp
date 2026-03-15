/*! Migration steps for revision 0.0.5-002. */

#include "Migration_0_0_5_002.h"

#include <QJsonArray>

namespace qcai2::Migration
{

namespace
{

void appendUniqueStrings(QJsonArray &target, const QJsonArray &source)
{
    for (const QJsonValue &value : source)
    {
        if (value.isString() == false)
        {
            continue;
        }

        const QString path = value.toString();
        bool exists = false;
        for (const QJsonValue &existing : target)
        {
            if (((existing.toString() == path) == true))
            {
                exists = true;
                break;
            }
        }

        if (exists == false)
        {
            target.append(path);
        }
    }
}

}  // namespace

bool migrateProjectStateTo_0_0_5_002(QJsonObject &root)
{
    if (((!root.contains(QStringLiteral("excludedDefaultLinkedFiles"))) == true))
    {
        return false;
    }

    QJsonArray ignoredLinkedFiles;
    appendUniqueStrings(ignoredLinkedFiles,
                        root.value(QStringLiteral("ignoredLinkedFiles")).toArray());
    appendUniqueStrings(ignoredLinkedFiles,
                        root.value(QStringLiteral("excludedDefaultLinkedFiles")).toArray());

    root[QStringLiteral("ignoredLinkedFiles")] = ignoredLinkedFiles;
    root.remove(QStringLiteral("excludedDefaultLinkedFiles"));
    return true;
}

}  // namespace qcai2::Migration
