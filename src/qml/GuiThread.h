#pragma once

#include <QMetaObject>
#include <QObject>
#include <Qt>

#include <functional>
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
// MegaSdkClient::shutdown()'s stop point instead.
inline void invokeOnGuiThread(QObject* target, std::function<void()> fn)
{
    QMetaObject::invokeMethod(target, std::move(fn), Qt::QueuedConnection);
}
