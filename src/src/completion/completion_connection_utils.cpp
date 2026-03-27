/*! @file
    @brief Implements helpers that apply completion-specific provider connection overrides.
*/

#include "completion_connection_utils.h"

#include "../providers/copilot_provider.h"
#include "../providers/iai_provider.h"
#include "../providers/local_http_provider.h"

#include <QMap>
#include <QRegularExpression>

namespace qcai2
{

namespace
{

QString safe_value(const QString &value)
{
    return value.trimmed().isEmpty() == false ? value.trimmed() : QStringLiteral("<empty>");
}

QMap<QString, QString> parse_custom_headers(const QString &text)
{
    QMap<QString, QString> headers;
    const QString normalized = text;
    const QStringList entries =
        normalized.split(QRegularExpression(QStringLiteral("[,\n]")), Qt::SkipEmptyParts);
    for (const QString &entry : entries)
    {
        const qsizetype separator_index = entry.indexOf(QLatin1Char(':'));
        if (separator_index <= 0)
        {
            continue;
        }

        const QString key = entry.left(separator_index).trimmed();
        const QString value = entry.mid(separator_index + 1).trimmed();
        if (key.isEmpty() == true || value.isEmpty() == true)
        {
            continue;
        }
        headers.insert(key, value);
    }
    return headers;
}

}  // namespace

QString describe_completion_connection_settings(const completion_connection_settings_t &settings)
{
    const QString provider_id = settings.provider_id.trimmed();
    if (provider_id == QStringLiteral("openai") || provider_id == QStringLiteral("anthropic"))
    {
        return QStringLiteral("provider=%1 base_url=%2 api_key_present=%3")
            .arg(safe_value(provider_id), safe_value(settings.base_url),
                 settings.api_key.isEmpty() == false ? QStringLiteral("yes")
                                                     : QStringLiteral("no"));
    }
    if (provider_id == QStringLiteral("local"))
    {
        return QStringLiteral("provider=%1 base_url=%2 endpoint_path=%3 api_key_present=%4 "
                              "custom_headers=%5")
            .arg(safe_value(provider_id), safe_value(settings.local_base_url),
                 safe_value(settings.local_endpoint_path),
                 settings.api_key.isEmpty() == false ? QStringLiteral("yes")
                                                     : QStringLiteral("no"))
            .arg(QString::number(parse_custom_headers(settings.local_custom_headers).size()));
    }
    if (provider_id == QStringLiteral("ollama"))
    {
        return QStringLiteral("provider=%1 base_url=%2")
            .arg(safe_value(provider_id), safe_value(settings.ollama_base_url));
    }
    if (provider_id == QStringLiteral("copilot"))
    {
        return QStringLiteral("provider=%1 node_path=%2 sidecar_path=%3")
            .arg(safe_value(provider_id), safe_value(settings.copilot_node_path),
                 safe_value(settings.copilot_sidecar_path));
    }
    return QStringLiteral("provider=%1").arg(safe_value(provider_id));
}

void apply_completion_connection_settings(iai_provider_t *provider,
                                          const completion_connection_settings_t &settings)
{
    if (provider == nullptr)
    {
        return;
    }

    const QString provider_id = provider->id();
    if (provider_id == QStringLiteral("openai") || provider_id == QStringLiteral("anthropic"))
    {
        provider->set_base_url(settings.base_url);
        provider->set_api_key(settings.api_key);
        return;
    }
    if (provider_id == QStringLiteral("local"))
    {
        provider->set_base_url(settings.local_base_url);
        provider->set_api_key(settings.api_key);
        if (auto *local_provider = dynamic_cast<local_http_provider_t *>(provider);
            local_provider != nullptr)
        {
            local_provider->set_endpoint_path(settings.local_endpoint_path);
            local_provider->set_custom_headers(
                parse_custom_headers(settings.local_custom_headers));
        }
        return;
    }
    if (provider_id == QStringLiteral("ollama"))
    {
        provider->set_base_url(settings.ollama_base_url);
        return;
    }
    if (provider_id == QStringLiteral("copilot"))
    {
        if (auto *copilot_provider = dynamic_cast<copilot_provider_t *>(provider);
            copilot_provider != nullptr)
        {
            copilot_provider->set_node_path(settings.copilot_node_path.isEmpty() == false
                                                ? settings.copilot_node_path
                                                : QStringLiteral("node"));
            copilot_provider->set_sidecar_path(settings.copilot_sidecar_path);
        }
    }
}

}  // namespace qcai2
