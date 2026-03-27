/*! @file
    @brief Implements the Qt signal-based facade for qompi completion sessions.
*/

#include "qompi/qt_completion_session.h"

#include "detail/callback_dispatcher.h"

#include <QPointer>
#include <QString>

#include <memory>
#include <string_view>
#include <utility>

namespace qompi::detail
{

/**
 * Creates a Qt-backed callback dispatcher.
 * @param target QObject whose thread should receive callbacks.
 * @return Dispatcher instance.
 */
std::shared_ptr<callback_dispatcher_t> create_qt_callback_dispatcher(QObject *target);

}  // namespace qompi::detail

namespace qompi
{

namespace
{

QString to_qstring(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

/**
 * Bridges core completion callbacks into Qt signal emissions.
 */
class qt_session_sink_t final : public completion_sink_t
{
public:
    /**
     * Creates the sink.
     * @param owner Target QObject facade.
     * @param dispatcher Event-loop dispatcher.
     */
    qt_session_sink_t(qt_completion_session_t *owner,
                      std::shared_ptr<detail::callback_dispatcher_t> dispatcher)
        : owner(owner), dispatcher(std::move(dispatcher))
    {
    }

    /** @copydoc completion_sink_t::on_started */
    void on_started(const std::string &request_id) override
    {
        QPointer<qt_completion_session_t> owner_copy(this->owner);
        const QString request_id_value = to_qstring(request_id);
        this->dispatcher->dispatch([owner_copy, request_id_value]() {
            if (owner_copy != nullptr)
            {
                emit owner_copy->started(request_id_value);
            }
        });
    }

    /** @copydoc completion_sink_t::on_chunk */
    void on_chunk(const completion_chunk_t &chunk) override
    {
        QPointer<qt_completion_session_t> owner_copy(this->owner);
        const QString text_delta = to_qstring(chunk.text_delta);
        const QString stable_text = to_qstring(chunk.stable_text);
        const bool is_final = chunk.is_final;
        this->dispatcher->dispatch([owner_copy, text_delta, stable_text, is_final]() {
            if (owner_copy != nullptr)
            {
                emit owner_copy->chunk_ready(text_delta, stable_text, is_final);
            }
        });
    }

    /** @copydoc completion_sink_t::on_completed */
    void on_completed(const completion_result_t &result) override
    {
        QPointer<qt_completion_session_t> owner_copy(this->owner);
        const QString request_id_value = to_qstring(result.request_id);
        const QString text = to_qstring(result.text);
        const QString finish_reason = to_qstring(result.finish_reason);
        this->dispatcher->dispatch([owner_copy, request_id_value, text, finish_reason]() {
            if (owner_copy != nullptr)
            {
                emit owner_copy->completed(request_id_value, text, finish_reason);
            }
        });
    }

    /** @copydoc completion_sink_t::on_failed */
    void on_failed(const completion_error_t &error) override
    {
        QPointer<qt_completion_session_t> owner_copy(this->owner);
        const int code = error.code;
        const QString message = to_qstring(error.message);
        const bool is_retryable = error.is_retryable;
        const bool is_cancellation = error.is_cancellation;
        this->dispatcher->dispatch([owner_copy, code, message, is_retryable, is_cancellation]() {
            if (owner_copy != nullptr)
            {
                emit owner_copy->failed(code, message, is_retryable, is_cancellation);
            }
        });
    }

private:
    QPointer<qt_completion_session_t> owner;
    std::shared_ptr<detail::callback_dispatcher_t> dispatcher;
};

}  // namespace

class qt_completion_session_t::private_t
{
public:
    std::shared_ptr<completion_session_t> session;
    std::shared_ptr<detail::callback_dispatcher_t> dispatcher;
    std::shared_ptr<completion_operation_t> active_operation;
};

qt_completion_session_t::qt_completion_session_t(std::shared_ptr<completion_session_t> session,
                                                 QObject *parent)
    : QObject(parent), d(std::make_unique<private_t>())
{
    this->d->session = std::move(session);
    this->d->dispatcher = detail::create_qt_callback_dispatcher(this);
}

qt_completion_session_t::~qt_completion_session_t() = default;

void qt_completion_session_t::request_completion(const completion_request_t &request,
                                                 const completion_options_t &options)
{
    if (this->d->session == nullptr)
    {
        return;
    }

    const auto sink = std::make_shared<qt_session_sink_t>(this, this->d->dispatcher);
    this->d->active_operation = this->d->session->request_completion(request, options, sink);
}

void qt_completion_session_t::cancel_active()
{
    if (this->d->active_operation != nullptr)
    {
        this->d->active_operation->cancel();
        return;
    }
    if (this->d->session != nullptr)
    {
        this->d->session->cancel_active();
    }
}

bool qt_completion_session_t::is_busy() const
{
    if (this->d->active_operation == nullptr)
    {
        return false;
    }
    return this->d->active_operation->is_terminal() == false;
}

}  // namespace qompi
