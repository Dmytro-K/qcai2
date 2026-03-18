/*! Implements reusable MCP server configuration JSON serialization helpers. */

#include <qtmcp/server_definition.h>

#include <QJsonArray>

namespace qtmcp
{

namespace
{

QJsonObject string_map_to_json(const QMap<QString, QString> &map)
{
    QJsonObject object;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        object.insert(it.key(), it.value());
    }
    return object;
}

bool string_map_from_json(const QJsonValue &value, QMap<QString, QString> *map, QString *error,
                          const QString &field_name)
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
            *error = QStringLiteral("Field '%1' must be an object.").arg(field_name);
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
                    QStringLiteral("Field '%1.%2' must be a string.").arg(field_name, it.key());
            }
            return false;
        }
        map->insert(it.key(), it.value().toString());
    }
    return true;
}

bool string_list_from_json(const QJsonValue &value, QStringList *list, QString *error,
                           const QString &field_name)
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
            *error = QStringLiteral("Field '%1' must be an array.").arg(field_name);
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
                *error = QStringLiteral("Field '%1[%2]' must be a string.").arg(field_name).arg(i);
            }
            return false;
        }
        list->append(array.at(i).toString());
    }
    return true;
}

}  // namespace

QJsonObject server_definition_to_json(const server_definition_t &definition)
{
    QJsonObject object = definition.extra_fields;
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
        object.insert(QStringLiteral("env"), string_map_to_json(definition.env));
    }
    object.insert(QStringLiteral("startupTimeoutMs"), definition.startup_timeout_ms);
    object.insert(QStringLiteral("requestTimeoutMs"), definition.request_timeout_ms);
    if (((!definition.url.isEmpty()) == true))
    {
        object.insert(QStringLiteral("url"), definition.url);
    }
    if (((!definition.headers.isEmpty()) == true))
    {
        object.insert(QStringLiteral("headers"), string_map_to_json(definition.headers));
    }
    return object;
}

bool server_definition_from_json(const QJsonObject &json, server_definition_t *definition,
                                 QString *error)
{
    if (((definition == nullptr) == true))
    {
        return false;
    }

    server_definition_t parsed;
    parsed.extra_fields = json;

    const QJsonValue enabled_value = json.value(QStringLiteral("enabled"));
    if (((!enabled_value.isUndefined() && !enabled_value.isBool()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'enabled' must be a boolean.");
        }
        return false;
    }
    if (enabled_value.isBool() == true)
    {
        parsed.enabled = enabled_value.toBool();
    }

    const QJsonValue transport_value = json.value(QStringLiteral("transport"));
    if (((!transport_value.isUndefined() && !transport_value.isString()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'transport' must be a string.");
        }
        return false;
    }
    if (transport_value.isString() == true)
    {
        parsed.transport = transport_value.toString();
    }

    const QJsonValue command_value = json.value(QStringLiteral("command"));
    if (((!command_value.isUndefined() && !command_value.isString()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'command' must be a string.");
        }
        return false;
    }
    if (command_value.isString() == true)
    {
        parsed.command = command_value.toString();
    }

    const QJsonValue url_value = json.value(QStringLiteral("url"));
    if (((!url_value.isUndefined() && !url_value.isString()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'url' must be a string.");
        }
        return false;
    }
    if (url_value.isString() == true)
    {
        parsed.url = url_value.toString();
    }

    const QJsonValue startup_timeout_value = json.value(QStringLiteral("startupTimeoutMs"));
    if (((!startup_timeout_value.isUndefined() && !startup_timeout_value.isDouble()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'startupTimeoutMs' must be a number.");
        }
        return false;
    }
    if (startup_timeout_value.isDouble() == true)
    {
        parsed.startup_timeout_ms = startup_timeout_value.toInt(parsed.startup_timeout_ms);
    }

    const QJsonValue request_timeout_value = json.value(QStringLiteral("requestTimeoutMs"));
    if (((!request_timeout_value.isUndefined() && !request_timeout_value.isDouble()) == true))
    {
        if (((error != nullptr) == true))
        {
            *error = QStringLiteral("Field 'requestTimeoutMs' must be a number.");
        }
        return false;
    }
    if (request_timeout_value.isDouble() == true)
    {
        parsed.request_timeout_ms = request_timeout_value.toInt(parsed.request_timeout_ms);
    }

    if (!string_list_from_json(json.value(QStringLiteral("args")), &parsed.args, error,
                               QStringLiteral("args")))
    {
        return false;
    }

    if (!string_map_from_json(json.value(QStringLiteral("env")), &parsed.env, error,
                              QStringLiteral("env")))
    {
        return false;
    }

    if (!string_map_from_json(json.value(QStringLiteral("headers")), &parsed.headers, error,
                              QStringLiteral("headers")))
    {
        return false;
    }

    parsed.extra_fields.remove(QStringLiteral("enabled"));
    parsed.extra_fields.remove(QStringLiteral("transport"));
    parsed.extra_fields.remove(QStringLiteral("command"));
    parsed.extra_fields.remove(QStringLiteral("args"));
    parsed.extra_fields.remove(QStringLiteral("env"));
    parsed.extra_fields.remove(QStringLiteral("startupTimeoutMs"));
    parsed.extra_fields.remove(QStringLiteral("requestTimeoutMs"));
    parsed.extra_fields.remove(QStringLiteral("url"));
    parsed.extra_fields.remove(QStringLiteral("headers"));

    *definition = parsed;
    return true;
}

QJsonObject server_definitions_to_json(const server_definitions_t &definitions)
{
    QJsonObject object;
    for (auto it = definitions.cbegin(); it != definitions.cend(); ++it)
    {
        object.insert(it.key(), server_definition_to_json(it.value()));
    }
    return object;
}

bool server_definitions_from_json(const QJsonObject &json, server_definitions_t *definitions,
                                  QString *error)
{
    if (((definitions == nullptr) == true))
    {
        return false;
    }

    server_definitions_t parsed;
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

        server_definition_t definition;
        QString definition_error;
        if (((!server_definition_from_json(it.value().toObject(), &definition,
                                           &definition_error)) == true))
        {
            if (((error != nullptr) == true))
            {
                *error =
                    QStringLiteral("Invalid MCP server '%1': %2").arg(it.key(), definition_error);
            }
            return false;
        }
        parsed.insert(it.key(), definition);
    }

    *definitions = parsed;
    return true;
}

}  // namespace qtmcp
