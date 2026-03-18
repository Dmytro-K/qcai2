/*! Declares reusable MCP server configuration and JSON serialization helpers. */
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace qtmcp
{

struct server_definition_t
{
    bool enabled = true;
    QString transport = QStringLiteral("stdio");
    QString command;
    QStringList args;
    QMap<QString, QString> env;
    int startup_timeout_ms = 10000;
    int request_timeout_ms = 30000;
    QString url;
    QMap<QString, QString> headers;
    QJsonObject extra_fields;

    friend bool operator==(const server_definition_t &, const server_definition_t &) = default;
};

using server_definitions_t = QMap<QString, server_definition_t>;

QJsonObject server_definition_to_json(const server_definition_t &definition);
bool server_definition_from_json(const QJsonObject &json, server_definition_t *definition,
                                 QString *error = nullptr);

QJsonObject server_definitions_to_json(const server_definitions_t &definitions);
bool server_definitions_from_json(const QJsonObject &json, server_definitions_t *definitions,
                                  QString *error = nullptr);

}  // namespace qtmcp
