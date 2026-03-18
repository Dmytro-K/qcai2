/*! Migration steps for revision 0.0.5-002. */

#include "Migration_0_0_5_002.h"

#include <QJsonArray>

namespace qcai2::Migration
{

namespace
{

void append_unique_strings(QJsonArray &target, const QJsonArray &source)
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

bool migrate_project_state_to_0_0_5_002(QJsonObject &root)
{
    if (((!root.contains(QStringLiteral("excludedDefaultLinkedFiles"))) == true))
    {
        return false;
    }

    QJsonArray ignored_linked_files;
    append_unique_strings(ignored_linked_files,
                          root.value(QStringLiteral("ignoredLinkedFiles")).toArray());
    append_unique_strings(ignored_linked_files,
                          root.value(QStringLiteral("excludedDefaultLinkedFiles")).toArray());

    root[QStringLiteral("ignoredLinkedFiles")] = ignored_linked_files;
    root.remove(QStringLiteral("excludedDefaultLinkedFiles"));
    return true;
}

}  // namespace qcai2::Migration
