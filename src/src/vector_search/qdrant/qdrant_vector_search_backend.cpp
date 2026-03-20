/*! Implements the Qdrant-backed vector search backend. */
#include "qdrant_vector_search_backend.h"

#include <qdrant_HttpRequest.h>

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

namespace qcai2
{

namespace
{

QString normalize_qdrant_path(const QString &base_path)
{
    QString normalized = base_path.trimmed();
    if (normalized.isEmpty())
    {
        return QStringLiteral("/");
    }
    if (normalized.endsWith(QLatin1Char('/')) == false)
    {
        normalized.append(QLatin1Char('/'));
    }
    return normalized;
}

QString api_key_status(const QString &api_key)
{
    return api_key.trimmed().isEmpty() ? QStringLiteral("absent") : QStringLiteral("present");
}

QUrl endpoint_for_collection(const qdrant_vector_search_config_t &config, const QString &suffix)
{
    QUrl url = QUrl::fromUserInput(config.base_url.trimmed());
    QString path = normalize_qdrant_path(url.path());
    path.append(QStringLiteral("collections/"));
    path.append(config.collection_name.trimmed());
    if (suffix.isEmpty() == false)
    {
        if (suffix.startsWith(QLatin1Char('/')) == false)
        {
            path.append(QLatin1Char('/'));
        }
        path.append(suffix);
    }
    url.setPath(path);
    return url;
}

QUrl endpoint_for_base(const qdrant_vector_search_config_t &config, const QString &suffix)
{
    QUrl url = QUrl::fromUserInput(config.base_url.trimmed());
    QString path = normalize_qdrant_path(url.path());
    if (suffix.startsWith(QLatin1Char('/')) == true)
    {
        path.append(suffix.sliced(1));
    }
    else
    {
        path.append(suffix);
    }
    url.setPath(path);
    return url;
}

QString format_qdrant_request_error(const QString &request_url,
                                    const qdrant_client::qdrant_HttpRequestWorker &worker)
{
    const int http_status = worker.getHttpResponseCode();
    if (http_status > 0)
    {
        return QStringLiteral("Qdrant returned HTTP %1 for %2: %3")
            .arg(http_status)
            .arg(request_url, worker.error_str);
    }

    return QStringLiteral("Request to %1 failed: %2").arg(request_url, worker.error_str);
}

QString certificate_status(const QString &path)
{
    return path.trimmed().isEmpty() ? QStringLiteral("absent") : QStringLiteral("present");
}

bool load_certificates_from_file(const QString &path, QList<QSslCertificate> *certificates,
                                 QString *error)
{
    if (certificates == nullptr)
    {
        return false;
    }

    certificates->clear();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Failed to open certificate file %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }

    QList<QSslCertificate> loaded = QSslCertificate::fromDevice(&file, QSsl::Pem);
    if (loaded.isEmpty() == true)
    {
        file.seek(0);
        loaded = QSslCertificate::fromDevice(&file, QSsl::Der);
    }

    if (loaded.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Certificate file %1 does not contain a valid PEM/DER "
                                    "certificate.")
                         .arg(path);
        }
        return false;
    }

    *certificates = std::move(loaded);
    return true;
}

bool try_load_private_key(const QString &path, QSsl::EncodingFormat format,
                          QSsl::KeyAlgorithm algorithm, QSslKey *private_key)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) == false)
    {
        return false;
    }

    const QSslKey candidate(&file, algorithm, format);
    if (candidate.isNull() == true)
    {
        return false;
    }

    if (private_key != nullptr)
    {
        *private_key = candidate;
    }
    return true;
}

bool load_client_private_key(const QString &client_key_file, QSslKey *private_key, QString *error)
{
    if (client_key_file.trimmed().isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Client key file must be configured when a client certificate "
                                    "file is provided.");
        }
        return false;
    }

    const QString trimmed_key_file = client_key_file.trimmed();
    if (try_load_private_key(trimmed_key_file, QSsl::Pem, QSsl::Ec, private_key) == true ||
        try_load_private_key(trimmed_key_file, QSsl::Pem, QSsl::Rsa, private_key) == true ||
        try_load_private_key(trimmed_key_file, QSsl::Der, QSsl::Ec, private_key) == true ||
        try_load_private_key(trimmed_key_file, QSsl::Der, QSsl::Rsa, private_key) == true)
    {
        return true;
    }

    if (error != nullptr)
    {
        *error = QStringLiteral("Client key file %1 does not contain a valid PEM/DER private key.")
                     .arg(trimmed_key_file);
    }
    return false;
}

bool build_ssl_configuration(const qdrant_vector_search_config_t &config,
                             QSslConfiguration *ssl_configuration, QString *error)
{
    if (ssl_configuration == nullptr)
    {
        return false;
    }

    QSslConfiguration configured = QSslConfiguration::defaultConfiguration();

    if (config.ca_certificate_file.trimmed().isEmpty() == false)
    {
        QList<QSslCertificate> ca_certificates;
        if (load_certificates_from_file(config.ca_certificate_file.trimmed(), &ca_certificates,
                                        error) == false)
        {
            return false;
        }

        QList<QSslCertificate> merged_ca_certificates = configured.caCertificates();
        merged_ca_certificates.append(ca_certificates);
        configured.setCaCertificates(merged_ca_certificates);
    }

    if (config.client_certificate_file.trimmed().isEmpty() == false)
    {
        QList<QSslCertificate> client_certificate_chain;
        if (load_certificates_from_file(config.client_certificate_file.trimmed(),
                                        &client_certificate_chain, error) == false)
        {
            return false;
        }

        QSslKey private_key;
        if (load_client_private_key(config.client_key_file.trimmed(), &private_key, error) ==
            false)
        {
            return false;
        }

        configured.setLocalCertificateChain(client_certificate_chain);
        configured.setLocalCertificate(client_certificate_chain.first());
        configured.setPrivateKey(private_key);
    }

    if (config.allow_self_signed_server_certificate == true)
    {
        configured.setPeerVerifyMode(QSslSocket::VerifyNone);
    }

    *ssl_configuration = configured;
    return true;
}

class scoped_ssl_default_configuration_t
{
public:
    explicit scoped_ssl_default_configuration_t(const QSslConfiguration &configuration)
        : previous_configuration(qdrant_client::qdrant_HttpRequestWorker::sslDefaultConfiguration),
          current_configuration(configuration)
    {
        qdrant_client::qdrant_HttpRequestWorker::sslDefaultConfiguration =
            &this->current_configuration;
    }

    ~scoped_ssl_default_configuration_t()
    {
        qdrant_client::qdrant_HttpRequestWorker::sslDefaultConfiguration =
            this->previous_configuration;
    }

private:
    QSslConfiguration *previous_configuration = nullptr;
    QSslConfiguration current_configuration;
};

bool execute_request(const qdrant_vector_search_config_t &config, const QString &request_url,
                     const QString &http_method, const QByteArray &request_body, int *status_code,
                     QByteArray *response_body, QString *error)
{
    QSslConfiguration ssl_configuration;
    QString ssl_error;
    if (build_ssl_configuration(config, &ssl_configuration, &ssl_error) == false)
    {
        if (error != nullptr)
        {
            *error = ssl_error;
        }
        return false;
    }

    qdrant_client::qdrant_HttpRequestInput request_input(request_url, http_method);
    if (config.api_key.trimmed().isEmpty() == false)
    {
        request_input.headers.insert(QStringLiteral("api-key"), config.api_key.trimmed());
    }
    if (request_body.isEmpty() == false)
    {
        request_input.headers.insert(QStringLiteral("Content-Type"),
                                     QStringLiteral("application/json"));
        request_input.request_body = request_body;
    }

    qdrant_client::qdrant_HttpRequestWorker worker;
    worker.setTimeOut(config.request_timeout_sec * 1000);
    scoped_ssl_default_configuration_t ssl_scope(ssl_configuration);

    QEventLoop loop;
    QTimer timeout_timer;
    bool timed_out = false;

    QObject::connect(&worker, &qdrant_client::qdrant_HttpRequestWorker::on_execution_finished,
                     &loop, [&loop](qdrant_client::qdrant_HttpRequestWorker *) { loop.quit(); });
    timeout_timer.setSingleShot(true);
    QObject::connect(&timeout_timer, &QTimer::timeout, &loop, [&]() {
        timed_out = true;
        loop.quit();
    });

    timeout_timer.start((config.request_timeout_sec * 1000) + 1000);
    worker.execute(&request_input);
    loop.exec();
    timeout_timer.stop();

    if (timed_out == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Timed out while connecting to %1").arg(request_url);
        }
        return false;
    }

    if (status_code != nullptr)
    {
        *status_code = worker.getHttpResponseCode();
    }
    if (response_body != nullptr)
    {
        *response_body = worker.response;
    }

    if ((worker.error_type != QNetworkReply::NoError) == true)
    {
        if (error != nullptr)
        {
            *error = format_qdrant_request_error(request_url, worker);
        }
        return false;
    }

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool execute_json_request(const qdrant_vector_search_config_t &config, const QString &request_url,
                          const QString &http_method, const QJsonObject &request_object,
                          int *status_code, QJsonDocument *response_document, QString *error)
{
    QByteArray response_body;
    if (execute_request(config, request_url, http_method, QJsonDocument(request_object).toJson(),
                        status_code, &response_body, error) == false)
    {
        return false;
    }

    if (response_document != nullptr)
    {
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(response_body, &parse_error);
        if ((response_body.trimmed().isEmpty() == false) &&
            (parse_error.error != QJsonParseError::NoError))
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Failed to parse Qdrant response JSON from %1: %2")
                             .arg(request_url, parse_error.errorString());
            }
            return false;
        }
        *response_document = document;
    }

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

QJsonArray float_list_to_json_array(const QList<float> &values)
{
    QJsonArray array;
    for (const float value : values)
    {
        array.append(static_cast<double>(value));
    }
    return array;
}

QJsonObject payload_to_filter(const QJsonObject &payload_filter)
{
    if (payload_filter.isEmpty() == true)
    {
        return {};
    }

    QJsonArray must_conditions;
    for (auto it = payload_filter.begin(); it != payload_filter.end(); ++it)
    {
        must_conditions.append(QJsonObject{
            {QStringLiteral("key"), it.key()},
            {QStringLiteral("match"), QJsonObject{{QStringLiteral("value"), it.value()}}},
        });
    }

    return QJsonObject{{QStringLiteral("must"), must_conditions}};
}

QString qdrant_point_id_to_string(const QJsonValue &value)
{
    if (value.isString() == true)
    {
        return value.toString();
    }
    if (value.isDouble() == true)
    {
        return QString::number(static_cast<qint64>(value.toDouble()));
    }
    if (value.isObject() == true)
    {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("uuid")) == true)
        {
            return object.value(QStringLiteral("uuid")).toString();
        }
        if (object.contains(QStringLiteral("num")) == true)
        {
            return QString::number(
                static_cast<qint64>(object.value(QStringLiteral("num")).toDouble()));
        }
    }
    return {};
}

QList<vector_search_match_t> parse_search_results(const QJsonDocument &document)
{
    QList<vector_search_match_t> matches;
    if (document.isObject() == false)
    {
        return matches;
    }

    const QJsonObject root = document.object();
    QJsonArray points;
    const QJsonValue result_value = root.value(QStringLiteral("result"));
    if (result_value.isArray() == true)
    {
        points = result_value.toArray();
    }
    else if (result_value.isObject() == true)
    {
        points = result_value.toObject().value(QStringLiteral("points")).toArray();
    }

    for (const QJsonValue &value : points)
    {
        if (value.isObject() == false)
        {
            continue;
        }
        const QJsonObject point = value.toObject();
        const QJsonObject payload = point.value(QStringLiteral("payload")).toObject();

        vector_search_match_t match;
        match.point_id = qdrant_point_id_to_string(point.value(QStringLiteral("id")));
        match.entity_kind = payload.value(QStringLiteral("entity_kind")).toString();
        match.workspace_root = payload.value(QStringLiteral("workspace_root")).toString();
        match.title = payload.value(QStringLiteral("title")).toString();
        match.file_path = payload.value(QStringLiteral("file_path")).toString();
        match.content = payload.value(QStringLiteral("content")).toString();
        match.line_start = payload.value(QStringLiteral("line_start")).toInt();
        match.line_end = payload.value(QStringLiteral("line_end")).toInt();
        match.score = point.value(QStringLiteral("score")).toDouble();
        match.payload = payload;
        matches.append(match);
    }
    return matches;
}

}  // namespace

qdrant_vector_search_config_t qdrant_vector_search_config_from_settings(const settings_t &settings)
{
    qdrant_vector_search_config_t config;
    config.base_url = settings.qdrant_url.trimmed();
    config.api_key = settings.qdrant_api_key;
    config.collection_name = settings.qdrant_collection_name.trimmed();
    config.auto_create_collection = settings.qdrant_auto_create_collection;
    config.ca_certificate_file = settings.qdrant_ca_certificate_file.trimmed();
    config.client_certificate_file = settings.qdrant_client_certificate_file.trimmed();
    config.client_key_file = settings.qdrant_client_key_file.trimmed();
    config.allow_self_signed_server_certificate =
        settings.qdrant_allow_self_signed_server_certificate;
    config.request_timeout_sec = settings.qdrant_timeout_sec;
    return config;
}

qdrant_vector_search_backend_t::qdrant_vector_search_backend_t(const settings_t &settings)
    : qdrant_vector_search_backend_t(qdrant_vector_search_config_from_settings(settings))
{
}

qdrant_vector_search_backend_t::qdrant_vector_search_backend_t(
    qdrant_vector_search_config_t config)
    : backend_config(std::move(config))
{
}

void qdrant_vector_search_backend_t::set_config(qdrant_vector_search_config_t config)
{
    this->backend_config = std::move(config);
}

const qdrant_vector_search_config_t &qdrant_vector_search_backend_t::config() const
{
    return this->backend_config;
}

QString qdrant_vector_search_backend_t::backend_id() const
{
    return QStringLiteral("qdrant");
}

QString qdrant_vector_search_backend_t::display_name() const
{
    return QStringLiteral("Qdrant");
}

bool qdrant_vector_search_backend_t::is_configured() const
{
    return this->backend_config.base_url.trimmed().isEmpty() == false &&
           this->backend_config.collection_name.trimmed().isEmpty() == false;
}

QString qdrant_vector_search_backend_t::configuration_summary() const
{
    return QStringLiteral("Qdrant [%1] collection=%2 api_key=%3 ca_cert=%4 client_cert=%5 "
                          "client_key=%6 auto_create=%7 self_signed=%8 timeout=%9s")
        .arg(this->backend_config.base_url.trimmed(),
             this->backend_config.collection_name.trimmed(),
             api_key_status(this->backend_config.api_key),
             certificate_status(this->backend_config.ca_certificate_file),
             certificate_status(this->backend_config.client_certificate_file),
             certificate_status(this->backend_config.client_key_file),
             this->backend_config.auto_create_collection ? QStringLiteral("enabled")
                                                         : QStringLiteral("disabled"),
             this->backend_config.allow_self_signed_server_certificate ? QStringLiteral("allowed")
                                                                       : QStringLiteral("strict"))
        .arg(this->backend_config.request_timeout_sec);
}

bool qdrant_vector_search_backend_t::check_connection(QString *error) const
{
    if (this->is_configured() == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant URL and collection name must be configured.");
        }
        return false;
    }

    const QString request_url = this->collections_list_endpoint().toString();
    int status_code = 0;
    if (execute_request(this->backend_config, request_url, QStringLiteral("GET"), {}, &status_code,
                        nullptr, error) == false)
    {
        return false;
    }
    if (((status_code < 200) || (status_code >= 300)) == true)
    {
        if (error != nullptr)
        {
            *error =
                QStringLiteral("Qdrant returned HTTP %1 for %2").arg(status_code).arg(request_url);
        }
        return false;
    }

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool qdrant_vector_search_backend_t::ensure_collection(int vector_dimensions, QString *error) const
{
    if ((this->is_configured() == false) || (vector_dimensions <= 0))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant URL, collection name, and vector dimensions must be "
                                    "configured.");
        }
        return false;
    }

    const QString get_url = this->collections_endpoint().toString();
    int status_code = 0;
    if (execute_request(this->backend_config, get_url, QStringLiteral("GET"), {}, &status_code,
                        nullptr, error) == false)
    {
        if ((status_code != 404) && (status_code != 0))
        {
            return false;
        }
    }

    if ((status_code >= 200) && (status_code < 300))
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    if (this->backend_config.auto_create_collection == false)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant collection %1 does not exist and auto-creation is "
                                    "disabled.")
                         .arg(this->backend_config.collection_name.trimmed());
        }
        return false;
    }

    const QJsonObject request{
        {QStringLiteral("vectors"),
         QJsonObject{
             {QStringLiteral("size"), vector_dimensions},
             {QStringLiteral("distance"), QStringLiteral("Cosine")},
         }},
    };

    if (execute_json_request(this->backend_config, get_url, QStringLiteral("PUT"), request,
                             &status_code, nullptr, error) == false)
    {
        return false;
    }

    if (((status_code < 200) || (status_code >= 300)) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant returned HTTP %1 while creating collection %2")
                         .arg(status_code)
                         .arg(this->backend_config.collection_name.trimmed());
        }
        return false;
    }

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool qdrant_vector_search_backend_t::upsert_points(const QJsonArray &points, int vector_dimensions,
                                                   QString *error) const
{
    if (points.isEmpty() == true)
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    if (this->ensure_collection(vector_dimensions, error) == false)
    {
        return false;
    }

    const QJsonObject request{{QStringLiteral("points"), points}};
    const QString request_url = this->upsert_points_endpoint().toString();
    int status_code = 0;
    if (execute_json_request(this->backend_config, request_url, QStringLiteral("PUT"), request,
                             &status_code, nullptr, error) == false)
    {
        return false;
    }

    if (((status_code < 200) || (status_code >= 300)) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant returned HTTP %1 while upserting points into %2")
                         .arg(status_code)
                         .arg(this->backend_config.collection_name.trimmed());
        }
        return false;
    }
    return true;
}

bool qdrant_vector_search_backend_t::delete_points(const QJsonObject &filter, QString *error) const
{
    if (filter.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant delete filter must not be empty.");
        }
        return false;
    }

    const QJsonObject request{{QStringLiteral("filter"), filter}};
    const QString request_url = this->delete_points_endpoint().toString();
    int status_code = 0;
    if (execute_json_request(this->backend_config, request_url, QStringLiteral("POST"), request,
                             &status_code, nullptr, error) == false)
    {
        return false;
    }

    if (((status_code < 200) || (status_code >= 300)) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant returned HTTP %1 while deleting points from %2")
                         .arg(status_code)
                         .arg(this->backend_config.collection_name.trimmed());
        }
        return false;
    }
    return true;
}

QList<vector_search_match_t>
qdrant_vector_search_backend_t::search(const QList<float> &query_vector, const QJsonObject &filter,
                                       int limit, QString *error) const
{
    if (query_vector.isEmpty() == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Query vector must not be empty.");
        }
        return {};
    }

    const QJsonObject request{
        {QStringLiteral("query"), float_list_to_json_array(query_vector)},
        {QStringLiteral("limit"), qMax(1, limit)},
        {QStringLiteral("with_payload"), true},
        {QStringLiteral("filter"), payload_to_filter(filter)},
    };

    const QString request_url = this->query_points_endpoint().toString();
    int status_code = 0;
    QJsonDocument response_document;
    if (execute_json_request(this->backend_config, request_url, QStringLiteral("POST"), request,
                             &status_code, &response_document, error) == false)
    {
        return {};
    }

    if (((status_code < 200) || (status_code >= 300)) == true)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Qdrant returned HTTP %1 while querying collection %2")
                         .arg(status_code)
                         .arg(this->backend_config.collection_name.trimmed());
        }
        return {};
    }

    if (error != nullptr)
    {
        error->clear();
    }
    return parse_search_results(response_document);
}

QUrl qdrant_vector_search_backend_t::collections_list_endpoint() const
{
    return endpoint_for_base(this->backend_config, QStringLiteral("collections"));
}

QUrl qdrant_vector_search_backend_t::collections_endpoint() const
{
    return endpoint_for_collection(this->backend_config, {});
}

QUrl qdrant_vector_search_backend_t::query_points_endpoint() const
{
    return endpoint_for_collection(this->backend_config, QStringLiteral("points/query"));
}

QUrl qdrant_vector_search_backend_t::upsert_points_endpoint() const
{
    QUrl url = endpoint_for_collection(this->backend_config, QStringLiteral("points"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("wait"), QStringLiteral("true"));
    url.setQuery(query);
    return url;
}

QUrl qdrant_vector_search_backend_t::delete_points_endpoint() const
{
    QUrl url = endpoint_for_collection(this->backend_config, QStringLiteral("points/delete"));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("wait"), QStringLiteral("true"));
    url.setQuery(query);
    return url;
}

}  // namespace qcai2
