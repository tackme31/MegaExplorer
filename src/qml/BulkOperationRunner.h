#pragma once
#include "core/Result.h"

#include <QVariantMap>

#include <functional>
#include <memory>

class BusyState;
class NotificationController;

// Bookkeeping for one bulk fan-out: N SDK calls from a single user action, of
// which only the last to land refreshes the listing and reports the tally.
//
// Deliberately does *not* own the fan-out loop, the GUI-thread hop or the
// shared_from_this() keep-alive -- those stay at the call site, where GuiThread.h's
// lifetime rules are visible. What it owns is the pairing with BusyState: start()
// takes the N begins, each settle() gives back one end.
//
// A Batch holds a reference to its runner, which references objects the owning
// controller owns, so it is only safe to settle while that controller is alive --
// guaranteed by the callback's shared_from_this() capture and by
// invokeOnGuiThread(this, ...) dropping queued calls when the controller dies.
class BulkOperationRunner
{
public:
    class Batch
    {
    public:
        // Counts one outcome and, once the batch is empty, refreshes, reports the
        // tally and runs onComplete, in that order. Must run on the GUI thread.
        void settle(const Result<void>& result);

    private:
        friend class BulkOperationRunner;
        Batch(BulkOperationRunner& runner,
              const char* context,
              int count,
              std::function<void()> refresh,
              std::function<void(int, int)> onComplete,
              QVariantMap undo);

        BulkOperationRunner& mRunner;
        // Names the operation in the log and the notification, so it must outlive the
        // batch: a string literal at every call site.
        const char* mContext;
        int mRemaining;
        int mSucceeded = 0;
        int mFailed = 0;
        std::function<void()> mRefresh;
        std::function<void(int, int)> mOnComplete;
        QVariantMap mUndo;
    };

    // defaultRefresh is what a batch re-reads when it doesn't bring its own.
    BulkOperationRunner(BusyState& busy,
                        NotificationController& notifications,
                        std::function<void()> defaultRefresh);

    // Takes all count begins on the busy state up front rather than one per issued
    // call: the calls go out from one GUI-thread loop and every completion returns
    // through invokeOnGuiThread, so no end() can interleave, and the N:N pairing
    // stays checkable in this file alone.
    //
    // count must be at least 1: a zero-count batch never settles, so it would hold a
    // begin forever and never report.
    //
    // refresh overrides the default "re-read what this tab is showing" -- a Ctrl+drop
    // onto another folder has nothing to re-read here. onComplete runs after the
    // refresh and the notification; both may be empty.
    //
    // undo rides along untouched to NotificationController::notifyOperation; see
    // there for what it means.
    std::shared_ptr<Batch> start(const char* context,
                                 int count,
                                 std::function<void()> refresh = {},
                                 std::function<void(int, int)> onComplete = {},
                                 QVariantMap undo = {});

private:
    BusyState& mBusy;
    NotificationController& mNotifications;
    std::function<void()> mDefaultRefresh;
};
