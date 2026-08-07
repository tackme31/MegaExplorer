#pragma once
#include "core/Result.h"

#include <functional>
#include <memory>

class BusyState;
class NotificationController;

// Bookkeeping for one bulk fan-out: N SDK calls issued from a single user
// action, of which only the last one to land refreshes the listing and reports
// the tally, so N operations produce one refetch and one notification
// (REFACTOR_PLANS.md's R5-4).
//
// Deliberately does *not* own the fan-out loop, the GUI-thread hop, or the
// shared_from_this() keep-alive: those stay at the call site, where the
// lifetime rules of GuiThread.h are visible. What it owns is the pairing
// between the batch and BusyState -- start() takes the N begins, each settle()
// gives back one end -- which is the part that was previously spread over three
// call sites and a private helper.
//
// Lifetime: a Batch holds a reference back to its runner, which holds
// references to objects the owning controller owns (its BusyState, its
// NotificationController, a default-refresh closure capturing the controller).
// A Batch is therefore only safe to settle while that controller is alive --
// guaranteed by the `self = shared_from_this()` capture in the callback that
// carries it, and by invokeOnGuiThread(this, ...) dropping queued calls when
// the controller dies first.
class BulkOperationRunner
{
public:
    class Batch
    {
    public:
        // Counts one outcome and, once the batch is empty, refreshes, reports
        // the tally and runs onComplete -- in that order. Must run on the GUI
        // thread; callers wrap it in invokeOnGuiThread.
        void settle(const Result<void>& result);

    private:
        friend class BulkOperationRunner;
        Batch(BulkOperationRunner& runner,
              const char* context,
              int count,
              std::function<void()> refresh,
              std::function<void(int, int)> onComplete);

        BulkOperationRunner& mRunner;
        // Names the operation in both the warning log and the notification, so
        // it must outlive the batch: a string literal at every call site.
        const char* mContext;
        int mRemaining;
        int mSucceeded = 0;
        int mFailed = 0;
        std::function<void()> mRefresh;
        std::function<void(int, int)> mOnComplete;
    };

    // defaultRefresh is what a batch re-reads when it doesn't bring its own.
    BulkOperationRunner(BusyState& busy,
                        NotificationController& notifications,
                        std::function<void()> defaultRefresh);

    // Opens a batch of count operations and takes all count begins on the busy
    // state up front, rather than one per issued call: the calls all go out
    // from one GUI-thread loop and every completion comes back through
    // invokeOnGuiThread, so no end() can interleave, and taking them together
    // is what makes the N:N pairing checkable in this file alone.
    //
    // count must be at least 1 -- a zero-count batch would never settle, so it
    // would hold a begin forever and never report. Every caller already returns
    // early on an empty selection.
    //
    // refresh overrides the default "re-read what this tab is showing": a copy
    // leaves the source folder alone, so a Ctrl+drop onto some other folder has
    // nothing to re-read here. onComplete runs after the refresh and the
    // notification, for announcing where the nodes went; both may be empty.
    std::shared_ptr<Batch> start(const char* context,
                                 int count,
                                 std::function<void()> refresh = {},
                                 std::function<void(int, int)> onComplete = {});

private:
    BusyState& mBusy;
    NotificationController& mNotifications;
    std::function<void()> mDefaultRefresh;
};
