#pragma once

#include <QMetaObject>
#include <QObject>
#include <Qt>
#include <QThread>

#include <functional>
#include <memory>
#include <utility>

// Service callbacks may fire on an SDK-internal background thread (see
// IMegaClient.h), so touching QML-facing state from one has to hop onto the
// GUI thread first. The single such helper in src/qml -- there is no qApp
// variant, deliberately.
//
// target is `this` at every call site: ~QObject drops any events still queued
// for it, so an object destroyed (tab closed) after this posts but before the
// GUI thread processes it simply loses the call instead of running fn against
// a dangling `this`.
//
// What it does NOT cover is the earlier window, between the SDK thread
// entering the outer lambda and this function posting the event. The per-tab
// controllers close it by capturing shared_from_this() in that outer lambda;
// the app-lifetime classes (main.cpp stack locals) rely on
// MegaSdkClient::shutdown()'s stop point instead. That capture keeps the
// object alive but says nothing about which thread finally destroys it --
// makeGuiOwned below is what answers that.
inline void invokeOnGuiThread(QObject* target, std::function<void()> fn)
{
    QMetaObject::invokeMethod(target, std::move(fn), Qt::QueuedConnection);
}

// Creates a shared_ptr-owned QObject whose destructor is forced back onto the
// object's own thread (the GUI thread, since that is where everything here is
// constructed), whichever thread happens to drop the last reference.
//
// Needed because the shared_from_this() copy described above lives in a
// closure owned by a MEGA SDK listener, and those listeners `delete this` on
// the SDK thread (MegaSdkClient.cpp). With the tab already closed, that copy
// is the last reference, so the destructor would run there: ~QTimer on a
// foreign thread takes the "Timers cannot be stopped from another thread"
// path and leaks the timer id, and ~QObject's removePostedEvents races the
// GUI thread draining the same queue. See REFACTOR_PLANS.md's R2-5.
//
// The same-thread branch is a plain delete, so the normal path (closing a tab
// on the GUI thread) is byte-for-byte what it was: the destructor still runs
// immediately and still drops queued callbacks via removePostedEvents.
//
// Accepted gap: if the last reference falls on the SDK thread during
// MegaSdkClient::shutdown() -- i.e. after app.exec() returned -- no event loop
// remains to deliver the deferred delete, and the object is simply never
// destroyed. That trades a crash for an at-exit leak of one controller.
// Flushing with sendPostedEvents(nullptr, QEvent::DeferredDelete) would close
// it but would also force every unrelated deleteLater in the QML engine.
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
