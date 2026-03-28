#include "../src/settings/settings.h"
#include "../src/vector_search/text_embedder.h"
#include "../src/vector_search/vector_search_service.h"

#if QCAI2_FEATURE_QDRANT_ENABLE
#include "../src/vector_search/qdrant/qdrant_vector_search_backend.h"
#endif

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace qcai2;

namespace
{

class http_test_server_t : public QObject
{
    Q_OBJECT

public:
    explicit http_test_server_t(int status_code, bool collection_exists = true,
                                QObject *parent = nullptr)
        : QObject(parent), response_status(status_code), collection_exists(collection_exists)
    {
        QObject::connect(&this->server, &QTcpServer::newConnection, this,
                         &http_test_server_t::handle_connection);
        const bool listening = this->server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(listening);
    }

    quint16 port() const
    {
        return this->server.serverPort();
    }

    bool collection_create_requested() const
    {
        return this->collection_created;
    }

private:
    void handle_connection()
    {
        QTcpSocket *socket = this->server.nextPendingConnection();
        QVERIFY(socket != nullptr);

        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray request = socket->readAll();
            if (request.contains("GET /collections ") == true)
            {
                const QByteArray response =
                    QByteArray("HTTP/1.1 ") + QByteArray::number(this->response_status) +
                    QByteArray(this->response_status == 200 ? " OK\r\n" : " Error\r\n") +
                    QByteArray("Content-Type: application/json\r\nConnection: "
                               "close\r\nContent-Length: 2\r\n\r\n{}");
                socket->write(response);
                socket->disconnectFromHost();
                return;
            }

            if (request.contains("GET /collections/qcai2-symbols ") == true)
            {
                const int status = this->collection_exists ? 200 : 404;
                const QByteArray response =
                    QByteArray("HTTP/1.1 ") + QByteArray::number(status) +
                    QByteArray(status == 200 ? " OK\r\n" : " Not Found\r\n") +
                    QByteArray("Content-Type: application/json\r\nConnection: "
                               "close\r\nContent-Length: 2\r\n\r\n{}");
                socket->write(response);
                socket->disconnectFromHost();
                return;
            }

            if (request.contains("PUT /collections/qcai2-symbols ") == true)
            {
                this->collection_created = true;
                this->collection_exists = true;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                              "close\r\nContent-Length: 2\r\n\r\n{}");
                socket->disconnectFromHost();
                return;
            }

            if (request.contains("PUT /collections/symbols-v1 ") == true)
            {
                socket->write(
                    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }

            socket->write(
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
    }

    QTcpServer server;
    int response_status = 200;
    bool collection_exists = true;
    bool collection_created = false;
};

#if QCAI2_FEATURE_QDRANT_ENABLE
settings_t configured_qdrant_settings(const QString &base_url)
{
    settings_t settings;
    settings.vector_search_enabled = true;
    settings.vector_search_provider = QStringLiteral("qdrant");
    settings.qdrant_url = base_url;
    settings.qdrant_collection_name = QStringLiteral("qcai2-symbols");
    settings.qdrant_timeout_sec = 2;
    return settings;
}
#endif

}  // namespace

class tst_vector_search_t : public QObject
{
    Q_OBJECT

private slots:
    void service_reports_disabled_state()
    {
        vector_search_service_t service;
        settings_t settings;
        settings.vector_search_enabled = false;
        service.apply_settings(settings);

        QVERIFY(!service.is_enabled());
        QVERIFY(!service.is_available());
        QCOMPARE(service.status_message(), QStringLiteral("Vector search disabled."));
    }

    void service_requires_configured_backend()
    {
        vector_search_service_t service;
        settings_t settings;
        settings.vector_search_enabled = true;
        settings.vector_search_provider = QStringLiteral("qdrant");
        settings.qdrant_url.clear();
        settings.qdrant_collection_name.clear();
        service.apply_settings(settings);

#if QCAI2_FEATURE_QDRANT_ENABLE
        QVERIFY(service.is_enabled());
        QVERIFY(!service.is_available());
        QCOMPARE(service.backend_id(), QStringLiteral("qdrant"));
        QCOMPARE(service.status_message(),
                 QStringLiteral("Vector search backend is not fully configured."));
        QString error;
        QVERIFY(!service.verify_connection(&error));
        QVERIFY(error.contains(QStringLiteral("Qdrant URL and collection name")));
#else
        QVERIFY(service.is_enabled());
        QVERIFY(!service.is_available());
        QCOMPARE(service.backend_id(), QString());
        QCOMPARE(service.status_message(),
                 QStringLiteral("Qdrant support is disabled at build time."));
        QString error;
        QVERIFY(!service.verify_connection(&error));
        QCOMPARE(error, QStringLiteral("Qdrant support is disabled at build time."));
        QCOMPARE(service.status_message(),
                 QStringLiteral("Qdrant support is disabled at build time."));
#endif
    }

#if QCAI2_FEATURE_QDRANT_ENABLE
    void qdrant_backend_uses_settings_values()
    {
        settings_t settings;
        settings.qdrant_url = QStringLiteral("http://qdrant.internal:6333/api");
        settings.qdrant_api_key = QStringLiteral("secret");
        settings.qdrant_collection_name = QStringLiteral("symbols-v1");
        settings.qdrant_auto_create_collection = true;
        settings.qdrant_ca_certificate_file = QStringLiteral("/tmp/ca.pem");
        settings.qdrant_client_certificate_file = QStringLiteral("/tmp/client.pem");
        settings.qdrant_client_key_file = QStringLiteral("/tmp/client.key");
        settings.qdrant_allow_self_signed_server_certificate = true;
        settings.qdrant_timeout_sec = 12;

        qdrant_vector_search_backend_t backend(settings);
        QVERIFY(backend.is_configured());
        QCOMPARE(backend.backend_id(), QStringLiteral("qdrant"));
        QCOMPARE(backend.display_name(), QStringLiteral("Qdrant"));
        QCOMPARE(backend.config().ca_certificate_file, QStringLiteral("/tmp/ca.pem"));
        QCOMPARE(backend.config().client_certificate_file, QStringLiteral("/tmp/client.pem"));
        QCOMPARE(backend.config().client_key_file, QStringLiteral("/tmp/client.key"));
        QVERIFY(backend.config().auto_create_collection);
        QVERIFY(backend.config().allow_self_signed_server_certificate);
        QCOMPARE(backend.collections_list_endpoint().toString(),
                 QStringLiteral("http://qdrant.internal:6333/api/collections"));
        QCOMPARE(backend.collections_endpoint().toString(),
                 QStringLiteral("http://qdrant.internal:6333/api/collections/symbols-v1"));
        QCOMPARE(
            backend.query_points_endpoint().toString(),
            QStringLiteral("http://qdrant.internal:6333/api/collections/symbols-v1/points/query"));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("api_key=present")));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("ca_cert=present")));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("client_cert=present")));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("client_key=present")));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("auto_create=enabled")));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("self_signed=allowed")));
        QVERIFY(backend.configuration_summary().contains(QStringLiteral("timeout=12s")));
    }

    void qdrant_backend_requires_url_and_collection()
    {
        qdrant_vector_search_config_t config;
        config.base_url.clear();

        qdrant_vector_search_backend_t backend(config);
        QVERIFY(!backend.is_configured());

        config.base_url = QStringLiteral("http://localhost:6333");
        config.collection_name.clear();
        backend.set_config(config);
        QVERIFY(!backend.is_configured());
    }

    void service_verifies_qdrant_connection_successfully()
    {
        http_test_server_t server(200);
        vector_search_service_t service;
        service.apply_settings(
            configured_qdrant_settings(QStringLiteral("http://127.0.0.1:%1").arg(server.port())));

        QString error;
        QVERIFY(service.verify_connection(&error));
        QVERIFY(error.isEmpty());
        QVERIFY(service.is_available());
        QCOMPARE(service.status_message(),
                 QStringLiteral("Vector search is available via Qdrant."));
    }

    void service_reports_qdrant_connection_failure()
    {
        vector_search_service_t service;
        service.apply_settings(configured_qdrant_settings(QStringLiteral("http://127.0.0.1:9")));

        QString error;
        QVERIFY(!service.verify_connection(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(service.is_enabled());
        QVERIFY(!service.is_available());
        QVERIFY(service.status_message().contains(QStringLiteral("Vector search unavailable:")));
    }

    void service_creates_missing_collection_when_enabled()
    {
        http_test_server_t server(200, false);
        vector_search_service_t service;
        settings_t settings =
            configured_qdrant_settings(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        settings.qdrant_auto_create_collection = true;
        service.apply_settings(settings);

        QString error;
        QVERIFY(service.verify_connection(&error));
        QVERIFY(service.ensure_collection(text_embedder_t::embedding_dimensions, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(server.collection_create_requested());
        QVERIFY(service.is_available());
    }

    void service_reports_missing_collection_when_auto_create_disabled()
    {
        http_test_server_t server(200, false);
        vector_search_service_t service;
        settings_t settings =
            configured_qdrant_settings(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        settings.qdrant_auto_create_collection = false;
        service.apply_settings(settings);

        QString error;
        QVERIFY(service.verify_connection(&error));
        QVERIFY(!service.ensure_collection(text_embedder_t::embedding_dimensions, &error));
        QVERIFY(error.contains(QStringLiteral("auto-creation is disabled")));
        QVERIFY(!service.is_available());
        QVERIFY(!server.collection_create_requested());
    }
#endif
};

QTEST_MAIN(tst_vector_search_t)

#include "tst_vector_search.moc"
