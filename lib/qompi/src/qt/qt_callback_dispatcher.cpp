/*! @file
    @brief Implements the Qt event-loop dispatcher used by the qompi Qt facade.
*/

#include "detail/callback_dispatcher.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <utility>

namespace qompi::detail
{

namespace
{

/**
 * Dispatches callbacks onto a QObject thread using queued invocations.
 */
class qt_callback_dispatcher_t final : public callback_dispatcher_t
{
public:
    /**
     * Creates the dispatcher.
     * @param target QObject whose thread owns the target event loop.
     */
    explicit qt_callback_dispatcher_t(QObject *target) : target(target)
    {
    }

    /** @copydoc callback_dispatcher_t::dispatch */
    void dispatch(std::function<void()> callback) override
    {
        if (this->target == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            this->target,
            [callback = std::move(callback)]() mutable {
                if (callback)
                {
                    callback();
                }
            },
            Qt::QueuedConnection);
    }

private:
    QPointer<QObject> target;
};

}  // namespace

/**
 * Creates a Qt-backed callback dispatcher.
 * @param target QObject whose thread should receive callbacks.
 * @return Dispatcher instance.
 */
std::shared_ptr<callback_dispatcher_t> create_qt_callback_dispatcher(QObject *target)
{
    return std::make_shared<qt_callback_dispatcher_t>(target);
}

}  // namespace qompi::detail
