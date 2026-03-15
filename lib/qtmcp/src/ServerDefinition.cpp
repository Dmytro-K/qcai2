/*! Implements reusable MCP server configuration JSON serialization helpers. */

#include <qtmcp/ServerDefinition.h>

#include <QJsonArray>

namespace qtmcp
{

namespace
{

QJsonObject stringMapToJson(const QMap<QString, QString> &map)
{
    QJsonObject object;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        object.insert(it.key(), it.value());
    }
    return object;
}

bool stringMapFromJson(const QJsonValue &value, QMap<QString, QString> *map, QString *error,
                       const QString &fieldName)
{
    if (((map == nullptr) == true))
    {
        return false;
    }

    map->clear();
    if (((value.isUndefined() || value.isNull()) == true))
    {
        return true;
    }
    if (value.isObject() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field '%1' must be an object.").arg(fieldName);
        }
        return false;
    }

    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); ((it != object.end()) == true); ++it)
    {
        if (((!it.value().isString()) == true))
        {
            if (((error != nullptr) == true))
            {
                *error =
                    QStringLiteral("Field '%1.%2' must be a string.").arg(fieldName, it.key());
            }
            return false;
        }
        map->insert(it.key(), it.value().toString());
    }
    return true;
}

bool stringListFromJson(const QJsonValue &value, QStringList *list, QString *error,
                        const QString &fieldName)
{
    if (((list == nullptr) == true))
    {
        return false;
    }

    list->clear();
    if (((value.isUndefined() || value.isNull()) == true))
    {
        return true;
    }
    if (value.isArray() == false)
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field '%1' must be an array.").arg(fieldName);
        }
        return false;
    }

    const QJsonArray array = value.toArray();
    list->reserve(array.size());
    for (int i = 0; i < array.size(); ++i)
    {
        if (((!array.at(i).isString()) == true))
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("Field '%1[%2]' must be a string.").arg(fieldName).arg(i);
            }
            return false;
        }
        list->append(array.at(i).toString());
    }
    return true;
}

}  // namespace

QJsonObject serverDefinitionToJson(const ServerDefinition &definition)
{
    QJsonObject object = definition.extraFields;
    object.insert(QStringLiteral("enabled"), definition.enabled);
    object.insert(QStringLiteral("transport"), definition.transport);
    if (((!definition.command.isEmpty()) == true))
    {
        object.insert(QStringLiteral("command"), definition.command);
    }
    if (((!definition.args.isEmpty()) == true))
    {
        object.insert(QStringLiteral("args"), QJsonArray::fromStringList(definition.args));
    }
    if (((!definition.env.isEmpty()) == true))
    {
        object.insert(QStringLiteral("env"), stringMapToJson(definition.env));
    }
    object.insert(QStringLiteral("startupTimeoutMs"), definition.startupTimeoutMs);
    object.insert(QStringLiteral("requestTimeoutMs"), definition.requestTimeoutMs);
    if (((!definition.url.isEmpty()) == true))
    {
        object.insert(QStringLiteral("url"), definition.url);
    }
    if (((!definition.headers.isEmpty()) == true))
    {
        object.insert(QStringLiteral("headers"), stringMapToJson(definition.headers));
    }
    return object;
}

bool serverDefinitionFromJson(const QJsonObject &json, ServerDefinition *definition,
                              QString *error)
{
    if (((definition == nullptr) == true))
    {
        return false;
    }

    ServerDefinition parsed;
    parsed.extraFields = json;

    const QJsonValue enabledValue = json.value(QStringLiteral("enabled"));
    if (((!enabledValue.isUndefined() && !enabledValue.isBool()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'enabled' must be a boolean.");
        }
        return false;
    }
    if (enabledValue.isBool() == true)
    {
        parsed.enabled = enabledValue.toBool();
    }

    const QJsonValue transportValue = json.value(QStringLiteral("transport"));
    if (((!transportValue.isUndefined() && !transportValue.isString()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'transport' must be a string.");
        }
        return false;
    }
    if (transportValue.isString() == true)
    {
        parsed.transport = transportValue.toString();
    }

    const QJsonValue commandValue = json.value(QStringLiteral("command"));
    if (((!commandValue.isUndefined() && !commandValue.isString()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'command' must be a string.");
        }
        return false;
    }
    if (commandValue.isString() == true)
    {
        parsed.command = commandValue.toString();
    }

    const QJsonValue urlValue = json.value(QStringLiteral("url"));
    if (((!urlValue.isUndefined() && !urlValue.isString()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'url' must be a string.");
        }
        return false;
    }
    if (urlValue.isString() == true)
    {
        parsed.url = urlValue.toString();
    }

    const QJsonValue startupTimeoutValue = json.value(QStringLiteral("startupTimeoutMs"));
    if (((!startupTimeoutValue.isUndefined() && !startupTimeoutValue.isDouble()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'startupTimeoutMs' must be a number.");
        }
        return false;
    }
    if (startupTimeoutValue.isDouble() == true)
    {
        parsed.startupTimeoutMs = startupTimeoutValue.toInt(parsed.startupTimeoutMs);
    }

    const QJsonValue requestTimeoutValue = json.value(QStringLiteral("requestTimeoutMs"));
    if (((!requestTimeoutValue.isUndefined() && !requestTimeoutValue.isDouble()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'requestTimeoutMs' must be a number.");
        }
        return false;
    }
    if (requestTimeoutValue.isDouble() == true)
    {
        parsed.requestTimeoutMs = requestTimeoutValue.toInt(parsed.requestTimeoutMs);
    }

    if (!stringListFromJson(json.value(QStringLiteral("args")), &parsed.args, error,
                            QStringLiteral("args")))
    {
        return false;
    }

    if (!stringMapFromJson(json.value(QStringLiteral("env")), &parsed.env, error,
                           QStringLiteral("env")))
    {
        return false;
    }

    if (!stringMapFromJson(json.value(QStringLiteral("headers")), &parsed.headers, error,
                           QStringLiteral("headers")))
    {
        return false;
    }

    parsed.extraFields.remove(QStringLiteral("enabled"));
    parsed.extraFields.remove(QStringLiteral("transport"));
    parsed.extraFields.remove(QStringLiteral("command"));
    parsed.extraFields.remove(QStringLiteral("args"));
    parsed.extraFields.remove(QStringLiteral("env"));
    parsed.extraFields.remove(QStringLiteral("startupTimeoutMs"));
    parsed.extraFields.remove(QStringLiteral("requestTimeoutMs"));
    parsed.extraFields.remove(QStringLiteral("url"));
    parsed.extraFields.remove(QStringLiteral("headers"));

    *definition = parsed;
    return true;
}

QJsonObject serverDefinitionsToJson(const ServerDefinitions &definitions)
{
    QJsonObject object;
    for (auto it = definitions.cbegin(); it != definitions.cend(); ++it)
    {
        object.insert(it.key(), serverDefinitionToJson(it.value()));
    }
    return object;
}

bool serverDefinitionsFromJson(const QJsonObject &json, ServerDefinitions *definitions,
                               QString *error)
{
    if (((definitions == nullptr) == true))
    {
        return false;
    }

    ServerDefinitions parsed;
    for (auto it = json.begin(); ((it != json.end()) == true); ++it)
    {
        if (((!it.value().isObject()) == true))
        {
            if (((error != nullptr) == true))
            {
                *error = QStringLiteral("MCP server '%1' must be an object.").arg(it.key());
            }
            return false;
        }

        ServerDefinition definition;
        QString definitionError;
        if (((!serverDefinitionFromJson(it.value().toObject(), &definition, &definitionError)) ==
             true))
        {
            if (((error != nullptr) == true))
            {
                *error =
                    QStringLiteral("Invalid MCP server '%1': %2").arg(it.key(), definitionError);
            }
            return false;
        }
        parsed.insert(it.key(), definition);
    }

    *definitions = parsed;
    return true;
}

}  // namespace qtmcp
