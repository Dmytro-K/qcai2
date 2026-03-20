/*! Declares the provider-agnostic vector search service facade. */
#pragma once

#include "../settings/settings.h"
#include "vector_search_backend.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <memory>

namespace qcai2
{

class vector_search_service_t : public QObject
{
    Q_OBJECT

public:
    explicit vector_search_service_t(QObject *parent = nullptr);

    void apply_settings(const settings_t &settings);
    void set_enabled(bool enabled);
    bool is_enabled() const;

    void set_backend(std::unique_ptr<vector_search_backend_t> backend);
    const vector_search_backend_t *backend() const;

    QString backend_id() const;
    bool is_available() const;
    QString configuration_summary() const;
    QString status_message() const;
    QString last_error() const;
    bool verify_connection(QString *error = nullptr);
    bool ensure_collection(int vector_dimensions, QString *error = nullptr);
    bool upsert_points(const QJsonArray &points, int vector_dimensions,
                       QString *error = nullptr) const;
    bool delete_points(const QJsonObject &filter, QString *error = nullptr) const;
    QList<vector_search_match_t> search(const QList<float> &query_vector,
                                        const QJsonObject &filter, int limit,
                                        QString *error = nullptr) const;

signals:
    void availability_changed(bool available, const QString &status_message);

private:
    void set_status(bool available, const QString &status_message);

    bool enabled = false;
    bool runtime_available = false;
    QString status;
    std::unique_ptr<vector_search_backend_t> current_backend;
};

vector_search_service_t &vector_search_service();

}  // namespace qcai2
