/*! Declares reusable MCP server configuration and JSON serialization helpers. */
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace qtmcp
{

struct ServerDefinition
{
    bool enabled = true;
    QString transport = QStringLiteral("stdio");
    QString command;
    QStringList args;
    QMap<QString, QString> env;
    int startupTimeoutMs = 10000;
    int requestTimeoutMs = 30000;
    QString url;
    QMap<QString, QString> headers;
    QJsonObject extraFields;

    friend bool operator==(const ServerDefinition &, const ServerDefinition &) = default;
};

using ServerDefinitions = QMap<QString, ServerDefinition>;

QJsonObject serverDefinitionToJson(const ServerDefinition &definition);
bool serverDefinitionFromJson(const QJsonObject &json, ServerDefinition *definition,
                              QString *error = nullptr);

QJsonObject serverDefinitionsToJson(const ServerDefinitions &definitions);
bool serverDefinitionsFromJson(const QJsonObject &json, ServerDefinitions *definitions,
                               QString *error = nullptr);

}  // namespace qtmcp
