/*! Implements the provider-agnostic vector search service facade. */
#include "vector_search_service.h"

#if QCAI2_FEATURE_QDRANT_ENABLE
#include "qdrant/qdrant_vector_search_backend.h"
#endif

#include <utility>

namespace qcai2
{

vector_search_service_t::vector_search_service_t(QObject *parent) : QObject(parent)
{
}

void vector_search_service_t::apply_settings(const settings_t &settings)
{
    this->enabled = settings.vector_search_enabled;
    this->current_backend.reset();

#if QCAI2_FEATURE_QDRANT_ENABLE
    if (settings.vector_search_provider == QStringLiteral("qdrant"))
    {
        this->current_backend = std::make_unique<qdrant_vector_search_backend_t>(settings);
    }
#else
    Q_UNUSED(settings);
#endif

    if (this->enabled == false)
    {
        this->set_status(false, QStringLiteral("Vector search disabled."));
        return;
    }

#if !QCAI2_FEATURE_QDRANT_ENABLE
    this->set_status(false, QStringLiteral("Qdrant support is disabled at build time."));
    return;
#endif

    if (this->current_backend == nullptr)
    {
        this->set_status(false,
                         QStringLiteral("Vector search enabled without an active backend."));
        return;
    }

    if (this->current_backend->is_configured() == false)
    {
        this->set_status(false, QStringLiteral("Vector search backend is not fully configured."));
        return;
    }

    this->set_status(false, QStringLiteral("Vector search is configured but not connected yet."));
}

void vector_search_service_t::set_enabled(bool enabled)
{
    this->enabled = enabled;
}

bool vector_search_service_t::is_enabled() const
{
    return this->enabled;
}

void vector_search_service_t::set_backend(std::unique_ptr<vector_search_backend_t> backend)
{
    this->current_backend = std::move(backend);
}

const vector_search_backend_t *vector_search_service_t::backend() const
{
    return this->current_backend.get();
}

QString vector_search_service_t::backend_id() const
{
    if (this->current_backend == nullptr)
    {
        return {};
    }
    return this->current_backend->backend_id();
}

bool vector_search_service_t::is_available() const
{
    return this->enabled && this->runtime_available && this->current_backend != nullptr &&
           this->current_backend->is_configured();
}

QString vector_search_service_t::configuration_summary() const
{
    if (this->enabled == false)
    {
        return QStringLiteral("Vector search disabled.");
    }
    if (this->current_backend == nullptr)
    {
#if !QCAI2_FEATURE_QDRANT_ENABLE
        return QStringLiteral("Qdrant support is disabled at build time.");
#endif
        return QStringLiteral("Vector search enabled without an active backend.");
    }
    return this->current_backend->configuration_summary();
}

QString vector_search_service_t::status_message() const
{
    return this->status;
}

QString vector_search_service_t::last_error() const
{
    return this->runtime_available ? QString() : this->status;
}

bool vector_search_service_t::verify_connection(QString *error)
{
    if (this->enabled == false)
    {
        this->set_status(false, QStringLiteral("Vector search disabled."));
        if (error != nullptr)
        {
            error->clear();
        }
        return false;
    }

    if (this->current_backend == nullptr)
    {
#if !QCAI2_FEATURE_QDRANT_ENABLE
        const QString unsupported_message =
            QStringLiteral("Qdrant support is disabled at build time.");
        this->set_status(false, unsupported_message);
        if (error != nullptr)
        {
            *error = unsupported_message;
        }
        return false;
#endif
        const QString message = QStringLiteral("Vector search enabled without an active backend.");
        this->set_status(false, message);
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    }

    QString connection_error;
    if (this->current_backend->check_connection(&connection_error) == false)
    {
        const QString message =
            QStringLiteral("Vector search unavailable: %1").arg(connection_error);
        this->set_status(false, message);
        if (error != nullptr)
        {
            *error = connection_error;
        }
        return false;
    }

    const QString message = QStringLiteral("Vector search is available via %1.")
                                .arg(this->current_backend->display_name());
    this->set_status(true, message);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool vector_search_service_t::ensure_collection(int vector_dimensions, QString *error)
{
    if (this->is_available() == false || this->current_backend == nullptr)
    {
        if (error != nullptr)
        {
            *error = this->last_error().isEmpty() ? QStringLiteral("Vector search is unavailable.")
                                                  : this->last_error();
        }
        return false;
    }

    QString ensure_error;
    if (this->current_backend->ensure_collection(vector_dimensions, &ensure_error) == false)
    {
        const QString message = QStringLiteral("Vector search unavailable: %1").arg(ensure_error);
        this->set_status(false, message);
        if (error != nullptr)
        {
            *error = ensure_error;
        }
        return false;
    }

    const QString message = QStringLiteral("Vector search is available via %1.")
                                .arg(this->current_backend->display_name());
    this->set_status(true, message);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool vector_search_service_t::upsert_points(const QJsonArray &points, int vector_dimensions,
                                            QString *error) const
{
    if (this->is_available() == false || this->current_backend == nullptr)
    {
        if (error != nullptr)
        {
            *error = this->last_error().isEmpty() ? QStringLiteral("Vector search is unavailable.")
                                                  : this->last_error();
        }
        return false;
    }
    return this->current_backend->upsert_points(points, vector_dimensions, error);
}

bool vector_search_service_t::delete_points(const QJsonObject &filter, QString *error) const
{
    if (this->is_available() == false || this->current_backend == nullptr)
    {
        if (error != nullptr)
        {
            *error = this->last_error().isEmpty() ? QStringLiteral("Vector search is unavailable.")
                                                  : this->last_error();
        }
        return false;
    }
    return this->current_backend->delete_points(filter, error);
}

QList<vector_search_match_t> vector_search_service_t::search(const QList<float> &query_vector,
                                                             const QJsonObject &filter, int limit,
                                                             QString *error) const
{
    if (this->is_available() == false || this->current_backend == nullptr)
    {
        if (error != nullptr)
        {
            *error = this->last_error().isEmpty() ? QStringLiteral("Vector search is unavailable.")
                                                  : this->last_error();
        }
        return {};
    }
    return this->current_backend->search(query_vector, filter, limit, error);
}

void vector_search_service_t::set_status(bool available, const QString &status_message)
{
    this->runtime_available = available;
    this->status = status_message;
    emit this->availability_changed(this->is_available(), this->status);
}

vector_search_service_t &vector_search_service()
{
    static vector_search_service_t service;
    return service;
}

}  // namespace qcai2
