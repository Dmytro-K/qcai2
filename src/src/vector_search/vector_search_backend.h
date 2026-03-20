/*! Declares the provider-agnostic vector search backend interface. */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace qcai2
{

struct vector_search_match_t
{
    QString point_id;
    QString entity_kind;
    QString workspace_root;
    QString title;
    QString file_path;
    QString content;
    double score = 0.0;
    int line_start = 0;
    int line_end = 0;
    QJsonObject payload;
};

class vector_search_backend_t
{
public:
    virtual ~vector_search_backend_t() = default;

    virtual QString backend_id() const = 0;
    virtual QString display_name() const = 0;
    virtual bool is_configured() const = 0;
    virtual QString configuration_summary() const = 0;
    virtual bool check_connection(QString *error = nullptr) const = 0;
    virtual bool ensure_collection(int vector_dimensions, QString *error = nullptr) const = 0;
    virtual bool upsert_points(const QJsonArray &points, int vector_dimensions,
                               QString *error = nullptr) const = 0;
    virtual bool delete_points(const QJsonObject &filter, QString *error = nullptr) const = 0;
    virtual QList<vector_search_match_t> search(const QList<float> &query_vector,
                                                const QJsonObject &filter, int limit,
                                                QString *error = nullptr) const = 0;
};

}  // namespace qcai2
