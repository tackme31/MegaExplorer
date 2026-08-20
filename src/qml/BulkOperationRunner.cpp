#include "BulkOperationRunner.h"

#include "app/Logging.h"
#include "BusyState.h"
#include "NotificationController.h"

#include <QString>

#include <utility>

BulkOperationRunner::Batch::Batch(BulkOperationRunner& runner,
                                  const char* context,
                                  int count,
                                  std::function<void()> refresh,
                                  std::function<void(int, int)> onComplete,
                                  QVariantMap undo)
    : mRunner(runner), mContext(context), mRemaining(count), mRefresh(std::move(refresh)),
      mOnComplete(std::move(onComplete)), mUndo(std::move(undo))
{}

void BulkOperationRunner::Batch::settle(const Result<void>& result)
{
    mRunner.mBusy.end();

    if (result.success)
    {
        ++mSucceeded;
    }
    else
    {
        ++mFailed;
        qCWarning(lcFileOps) << mContext << "failed:" << QString::fromStdString(result.errorMessage)
                             << "code=" << result.errorCode;
    }

    if (--mRemaining > 0)
        return;

    if (mRefresh)
        mRefresh();
    else
        mRunner.mDefaultRefresh();
    mRunner.mNotifications.notifyOperation(QString::fromLatin1(mContext),
                                           mSucceeded,
                                           mFailed,
                                           mUndo);
    if (mOnComplete)
        mOnComplete(mSucceeded, mFailed);
}

BulkOperationRunner::BulkOperationRunner(BusyState& busy,
                                         NotificationController& notifications,
                                         std::function<void()> defaultRefresh)
    : mBusy(busy), mNotifications(notifications), mDefaultRefresh(std::move(defaultRefresh))
{}

std::shared_ptr<BulkOperationRunner::Batch>
BulkOperationRunner::start(const char* context,
                           int count,
                           std::function<void()> refresh,
                           std::function<void(int, int)> onComplete,
                           QVariantMap undo)
{
    Q_ASSERT(count > 0);

    for (int i = 0; i < count; ++i)
        mBusy.begin();

    // make_shared can't reach the private constructor, and the batch is small
    // enough that the extra allocation the two-step form costs is irrelevant
    // next to the N server round-trips it is about to track.
    return std::shared_ptr<Batch>(new Batch(
        *this, context, count, std::move(refresh), std::move(onComplete), std::move(undo)));
}
