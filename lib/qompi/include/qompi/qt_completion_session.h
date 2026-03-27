/*! @file
    @brief Declares the Qt signal-based facade for qompi completion sessions.
*/

#pragma once

#include "completion_session.h"

#include <QObject>

#include <memory>

namespace qompi
{

/**
 * Adapts a transport-agnostic completion session to Qt signals.
 *
 * Qt-specific dispatch is intentionally isolated in the implementation so the core session and
 * provider layers can stay independent from Qt event-loop semantics.
 */
class qt_completion_session_t : public QObject
{
    Q_OBJECT

public:
    /**
     * Creates one Qt facade around an existing session implementation.
     * @param session Wrapped completion session.
     * @param parent QObject parent.
     */
    explicit qt_completion_session_t(std::shared_ptr<completion_session_t> session,
                                     QObject *parent = nullptr);

    /** Destructor. */
    ~qt_completion_session_t() override;

    /**
     * Starts one completion request through the wrapped session.
     * @param request Normalized request data.
     * @param options Per-request provider overrides.
     */
    void request_completion(const completion_request_t &request,
                            const completion_options_t &options = {});

    /**
     * Cancels the currently active request, if any.
     */
    void cancel_active();

    /**
     * Returns whether the facade still has one non-terminal active operation.
     * @return True when one request is still active.
     */
    bool is_busy() const;

signals:
    /** Emitted when one request starts. */
    void started(const QString &request_id);

    /** Emitted when one streamed chunk is available. */
    void chunk_ready(const QString &text_delta, const QString &stable_text, bool is_final);

    /** Emitted when one request completes successfully. */
    void completed(const QString &request_id, const QString &text, const QString &finish_reason);

    /** Emitted when one request fails. */
    void failed(int code, const QString &message, bool is_retryable, bool is_cancellation);

private:
    class private_t;
    std::unique_ptr<private_t> d;
};

}  // namespace qompi
