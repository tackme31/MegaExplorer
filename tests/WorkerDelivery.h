#pragma once
#include "TestApp.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEvent>

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Opt-in worker-thread delivery of IMegaClient callbacks, for the handful of
// tests that need one.
//
// Every other test fires completions with gmock's InvokeArgument, i.e.
// synchronously on the test's own thread, which leaves the cross-thread halves
// of src/qml/GuiThread.h unreachable -- makeGuiOwned's deleteLater branch never
// runs and invokeOnGuiThread is only ever called from the thread it posts to.
// That is REFACTOR_PLANS.md's R2-19.
//
// Deliberately NOT a mode on MockMegaClient: 18 test files and ~200
// InvokeArgument call sites share that mock, and all of them assert
// immediately after the act, so a mock-level switch would have to be threaded
// through every one of them. Here a test opts in by capturing the callback
// with a plain Invoke lambda (DownloadControllerTest already does this) and
// handing it to deliver(); nothing changes for a test that doesn't.
//
// What this buys is execution of the cross-thread paths, not detection of data
// races -- Windows has no ThreadSanitizer on either MSVC or clang-cl, so the
// mutexes in DownloadService/UploadService/ThumbnailService remain unverified.
class WorkerDelivery
{
public:
    WorkerDelivery() = default;
    WorkerDelivery(const WorkerDelivery&) = delete;
    WorkerDelivery& operator=(const WorkerDelivery&) = delete;

    // Joins here as well as in joinAll(): a worker still inside a service
    // callback when the mock or the service goes away would be reading freed
    // memory, and a race in the test harness itself is the one thing these
    // tests cannot afford. In a fixture, declare the WorkerDelivery member
    // LAST so it is destroyed FIRST.
    ~WorkerDelivery()
    {
        joinAll();
    }

    // Runs fn on a thread of its own and returns immediately, the way an SDK
    // listener delivers a completion from a thread the caller doesn't own.
    void deliver(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mThreads.emplace_back(std::move(fn));
    }

    void joinAll()
    {
        std::vector<std::thread> threads;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            threads.swap(mThreads);
        }
        for (std::thread& thread : threads)
        {
            if (thread.joinable())
                thread.join();
        }
    }

private:
    std::mutex mMutex;
    std::vector<std::thread> mThreads;
};

// Drains the GUI thread's queue until predicate holds, or the deadline passes.
// Returns predicate()'s final value so the caller can EXPECT on it.
//
// A fixed number of flushQueuedEvents() calls is enough when InvokeArgument
// fires the callback inline, but not here: a worker posts whenever it happens
// to be scheduled, which may be after the test's next statement.
inline bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 2000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!predicate())
    {
        if (deadline.hasExpired())
            return predicate();
        flushQueuedEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// deleteLater() posts a DeferredDelete event, and whether processEvents()
// delivers one depends on the event-loop nesting level it was posted at --
// these tests run outside exec() entirely. Asking for the type explicitly is
// unconditional, so the makeGuiOwned tests don't rest on that rule.
inline void flushDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
