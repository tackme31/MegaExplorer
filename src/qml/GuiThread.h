#pragma once

#include <QMetaObject>
#include <QObject>
#include <Qt>
#include <QThread>

#include <functional>
#include <memory>
#include <utility>

// Service callbacks may fire on an SDK-internal thread, so touching QML-facing
// state from one has to hop onto the GUI thread first.
//
// target is `this` at every call site: ~QObject drops any events still queued for
// it, so an object destroyed after this posts but before the GUI thread processes
// it loses the call instead of running fn against a dangling `this`.
//
// It does NOT cover the earlier window, between the SDK thread entering the outer
// lambda and this function posting. Per-tab controllers close that by capturing
// shared_from_this() there; app-lifetime classes rely on MegaSdkClient::shutdown()'s
// stop point. That capture keeps the object alive but says nothing about which
// thread destroys it -- makeGuiOwned answers that.
inline void invokeOnGuiThread(QObject* target, std::function<void()> fn)
{
    QMetaObject::invokeMethod(target, std::move(fn), Qt::QueuedConnection);
}

// A shared_ptr-owned QObject whose destructor is forced back onto the object's own
// (GUI) thread, whichever thread drops the last reference.
//
// Needed because that last reference is often the shared_from_this() copy inside a
// MEGA SDK listener, and those listeners `delete this` on the SDK thread. Running
// the destructor there means ~QTimer takes the "Timers cannot be stopped from
// another thread" path and leaks the timer id, while ~QObject's removePostedEvents
// races the GUI thread draining the same queue.
//
// The same-thread branch is a plain delete, so closing a tab on the GUI thread
// behaves exactly as before.
//
// Accepted gap: if the last reference falls on the SDK thread during
// MegaSdkClient::shutdown() -- after app.exec() returned -- no event loop remains
// to deliver the deferred delete and the object is never destroyed. That trades a
// crash for an at-exit leak. Flushing with sendPostedEvents(DeferredDelete) would
// close it but would also force every unrelated deleteLater in the QML engine.
template<typename T, typename... Args>
std::shared_ptr<T> makeGuiOwned(Args&&... args)
{
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), [](T* object) {
        if (object->thread() == QThread::currentThread())
            delete object;
        else
            object->deleteLater();
    });
}
