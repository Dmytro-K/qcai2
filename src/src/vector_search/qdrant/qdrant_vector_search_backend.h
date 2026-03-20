/*! Declares the Qdrant-backed vector search backend. */
#pragma once

#include "../vector_search_backend.h"

#include "../../settings/settings.h"

#include <QUrl>

namespace qcai2
{

struct qdrant_vector_search_config_t
{
    QString base_url = QStringLiteral("http://localhost:6333");
    QString api_key;
    QString collection_name = QStringLiteral("qcai2-symbols");
    bool auto_create_collection = true;
    QString ca_certificate_file;
    QString client_certificate_file;
    QString client_key_file;
    bool allow_self_signed_server_certificate = false;
    int request_timeout_sec = 30;
};

qdrant_vector_search_config_t
qdrant_vector_search_config_from_settings(const settings_t &settings);

class qdrant_vector_search_backend_t final : public vector_search_backend_t
{
public:
    explicit qdrant_vector_search_backend_t(const settings_t &settings);
    explicit qdrant_vector_search_backend_t(qdrant_vector_search_config_t config);

    void set_config(qdrant_vector_search_config_t config);
    const qdrant_vector_search_config_t &config() const;

    QString backend_id() const override;
    QString display_name() const override;
    bool is_configured() const override;
    QString configuration_summary() const override;
    bool check_connection(QString *error = nullptr) const override;
    bool ensure_collection(int vector_dimensions, QString *error = nullptr) const override;
    bool upsert_points(const QJsonArray &points, int vector_dimensions,
                       QString *error = nullptr) const override;
    bool delete_points(const QJsonObject &filter, QString *error = nullptr) const override;
    QList<vector_search_match_t> search(const QList<float> &query_vector,
                                        const QJsonObject &filter, int limit,
                                        QString *error = nullptr) const override;

    QUrl collections_list_endpoint() const;
    QUrl collections_endpoint() const;
    QUrl query_points_endpoint() const;
    QUrl upsert_points_endpoint() const;
    QUrl delete_points_endpoint() const;

private:
    qdrant_vector_search_config_t backend_config;
};

}  // namespace qcai2
