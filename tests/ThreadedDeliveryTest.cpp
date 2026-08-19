#include "core/DownloadService.h"
#include "core/FileEntry.h"
#include "core/ThumbnailService.h"
#include "MockMegaClient.h"
#include "qml/DownloadController.h"
#include "qml/FileListModel.h"
#include "qml/GuiThread.h"
#include "qml/NotificationController.h"
#include "qml/ThumbnailController.h"
#include "TestApp.h"
#include "WorkerDelivery.h"

#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The cross-thread half of src/qml/GuiThread.h, which no other test reaches:
// everywhere else the SDK callback is fired by InvokeArgument on the test's own
// thread, so makeGuiOwned's deleteLater branch never runs and invokeOnGuiThread
// only ever posts to the thread it was called from. See WorkerDelivery.h for
// why the opt-in lives in the action rather than in MockMegaClient.
//
// Only cases that fail *deterministically* when the product code loses its
// thread hop belong here. Detecting an actual data race would need a
// ThreadSanitizer, which neither MSVC nor clang-cl offers on Windows, so the
// service mutexes stay outside what these tests can claim.
namespace
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;

using DownloadDoneCallback = std::function<void(Result<DownloadOutcome>)>;
using ThumbnailDoneCallback = std::function<void(Result<std::string>)>;

// Records which thread destroyed it. No Q_OBJECT on purpose: nothing here
// needs signals, and adding one would pull moc into this file for nothing.
class DestructionProbe : public QObject
{
public:
    DestructionProbe(QThread** destroyedOn, std::atomic<bool>* destroyed)
        : mDestroyedOn(destroyedOn), mDestroyed(destroyed)
    {}

    ~DestructionProbe() override
    {
        *mDestroyedOn = QThread::currentThread();
        mDestroyed->store(true);
    }

private:
    QThread** mDestroyedOn;
    std::atomic<bool>* mDestroyed;
};

FileEntry thumbnailEntry(std::uint64_t handle)
{
    FileEntry entry;
    entry.name = "photo.jpg";
    entry.handle = handle;
    entry.hasThumbnail = true;
    return entry;
}

TEST(ThreadedDeliveryTest, MakeGuiOwnedDefersDestructionBackToTheOwningThread)
{
    // R2-5's fix, which until now had no automated verification at all. A MEGA
    // SDK listener does `delete this` on its own thread, so when the tab has
    // already closed, the shared_from_this() copy inside the listener's closure
    // is the last reference and the destructor would run there -- ~QTimer on a
    // foreign thread leaks the timer id, and ~QObject's removePostedEvents
    // races the GUI thread draining the same queue.
    QThread* destroyedOn = nullptr;
    std::atomic<bool> destroyed{false};
    WorkerDelivery delivery;

    {
        auto probe = makeGuiOwned<DestructionProbe>(&destroyedOn, &destroyed);
        delivery.deliver([probe = std::move(probe)]() mutable {
            probe.reset();
        });
    }
    delivery.joinAll();

    // The worker has exited and the object is still alive: deleteLater only
    // queued the destruction. A plain delete in the deleter would already have
    // run it, on the worker.
    EXPECT_FALSE(destroyed.load());

    flushDeferredDeletes();

    EXPECT_TRUE(destroyed.load());
    EXPECT_EQ(destroyedOn, QThread::currentThread());
}

TEST(ThreadedDeliveryTest, InvokeOnGuiThreadFromAWorkerRunsTheCallbackOnTheGuiThread)
{
    // The companion to TestMain.cpp's same-thread case: there the queued call
    // is posted from the thread that will run it, so a Qt::DirectConnection
    // would pass too.
    QObject target;
    QThread* ranOn = nullptr;
    std::atomic<bool> called{false};
    WorkerDelivery delivery;

    delivery.deliver([&target, &ranOn, &called]() {
        invokeOnGuiThread(&target, [&ranOn, &called]() {
            ranOn = QThread::currentThread();
            called.store(true);
        });
    });
    delivery.joinAll();

    EXPECT_TRUE(waitFor([&called]() {
        return called.load();
    }));
    EXPECT_EQ(ranOn, QThread::currentThread());
}

TEST(ThreadedDeliveryTest, QueuedCallFromAWorkerIsDroppedWhenTheTargetDiesFirst)
{
    // GuiThread.h's stated reason for passing `this` as the target: ~QObject
    // removes the events still queued for it, which is what makes capturing a
    // raw `this` in the inner lambda safe when a tab closes mid-flight.
    bool called = false;
    auto target = std::make_unique<QObject>();
    WorkerDelivery delivery;

    delivery.deliver([raw = target.get(), &called]() {
        invokeOnGuiThread(raw, [&called]() {
            called = true;
        });
    });
    delivery.joinAll(); // the event is definitely posted by now

    target.reset();
    flushQueuedEvents();

    EXPECT_FALSE(called);
}

TEST(ThreadedDeliveryTest, DownloadControllerEmitsOnTheGuiThreadWhenCompletionArrivesOnAWorker)
{
    // DownloadController::downloadFinished drives a QML snackbar, so emitting
    // it from an SDK thread would touch the scene graph off the GUI thread.
    // Deleting either invokeOnGuiThread in DownloadController.cpp fails this.
    auto client = std::make_shared<MockMegaClient>();
    DownloadDoneCallback onDone;
    EXPECT_CALL(*client, download(_, _, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([&onDone](std::uint64_t,
                                         const std::string&,
                                         std::function<void(std::uint64_t, std::uint64_t)>,
                                         DownloadDoneCallback done) {
            onDone = std::move(done);
        }));

    auto service = std::make_shared<DownloadService>(client);
    NotificationController notifications;
    DownloadController controller(service, &notifications);

    // Qt::DirectConnection, and not by accident: an auto-connected handler
    // whose context object lives on the GUI thread gets queued by Qt itself
    // when the signal is emitted from another thread, so it would report the
    // GUI thread no matter which thread did the emitting -- and pass even with
    // DownloadController's own hop deleted. Direct means this lambda runs on
    // whichever thread reached the emit.
    std::atomic<QThread*> emittedOn{nullptr};
    std::atomic<bool> finished{false};
    QObject::connect(
        &controller,
        &DownloadController::downloadFinished,
        &controller,
        [&emittedOn, &finished](bool, QString, QString) {
            emittedOn.store(QThread::currentThread());
            finished.store(true);
        },
        Qt::DirectConnection);

    WorkerDelivery delivery;
    controller.downloadFile(5, QStringLiteral("a.txt"), 100);
    ASSERT_TRUE(static_cast<bool>(onDone));

    // Act: the transfer completes on a thread the GUI never touches
    delivery.deliver([onDone]() {
        onDone(Result<DownloadOutcome>::ok(DownloadOutcome{"C:\\tmp\\a.txt"}));
    });
    delivery.joinAll();

    ASSERT_TRUE(waitFor([&finished]() {
        return finished.load();
    }));
    EXPECT_EQ(emittedOn.load(), QThread::currentThread());
}

TEST(ThreadedDeliveryTest, ClearingAnObserverStopsOnlyTheDeliveriesThatStartAfterIt)
{
    // Pins exactly how much ~DownloadController / ~UploadController buy by
    // clearing their observers, because the destructors' old comment claimed
    // more: DownloadService copies the observer under its lock and calls the
    // copy after unlocking, so clearing stops the next delivery but not one
    // already past the copy. That remaining window is closed by main.cpp's
    // client->shutdown() joining the SDK thread before either controller is
    // destroyed -- not by the clearing, and not by the null check on the copy.
    // The observer here is the test's own, since the uncovered half runs
    // against freed memory and so cannot be asserted on at all.
    auto client = std::make_shared<MockMegaClient>();
    std::function<void(std::uint64_t, std::uint64_t)> onProgress;
    EXPECT_CALL(*client, download(_, _, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(
            Invoke([&onProgress](std::uint64_t,
                                 const std::string&,
                                 std::function<void(std::uint64_t, std::uint64_t)> progress,
                                 DownloadDoneCallback) {
                onProgress = std::move(progress);
            }));

    auto service = std::make_shared<DownloadService>(client);
    std::atomic<int> observed{0};
    service->setOnProgress([&observed](DownloadJob) {
        observed.fetch_add(1);
    });
    service->enqueue(5, "a.txt", "C:\\tmp\\a.txt", 100);
    ASSERT_TRUE(static_cast<bool>(onProgress));

    WorkerDelivery delivery;
    delivery.deliver([onProgress]() {
        onProgress(50, 100);
    });
    delivery.joinAll();
    ASSERT_EQ(observed.load(), 1);

    service->setOnProgress(nullptr); // what the two destructors do

    delivery.deliver([onProgress]() {
        onProgress(75, 100);
    });
    delivery.joinAll();

    EXPECT_EQ(observed.load(), 1);
}

TEST(ThreadedDeliveryTest, ThumbnailControllerTouchesTheModelOnTheGuiThreadOnly)
{
    // The highest-consequence hop in src/qml: setThumbnailPath emits
    // dataChanged, and mutating a QAbstractItemModel from a worker corrupts the
    // view's own bookkeeping rather than merely racing a value.
    auto client = std::make_shared<MockMegaClient>();
    ThumbnailDoneCallback onDone;
    EXPECT_CALL(*client, getThumbnail(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(
            Invoke([&onDone](std::uint64_t, const std::string&, ThumbnailDoneCallback done) {
                onDone = std::move(done);
            }));

    auto service = std::make_shared<ThumbnailService>(client);
    auto model = std::make_shared<FileListModel>();
    model->setEntries({thumbnailEntry(5)});
    NotificationController notifications;
    // makeGuiOwned like main.cpp -- ThumbnailController uses shared_from_this()
    // in the callback, so it has to be shared_ptr-owned either way.
    auto controller = makeGuiOwned<ThumbnailController>(service, model, &notifications);

    // Qt::DirectConnection for the same reason as the DownloadController case
    // above -- an auto-connected handler would be queued onto the GUI thread by
    // Qt and report it regardless of where setThumbnailPath actually ran.
    std::atomic<QThread*> changedOn{nullptr};
    std::atomic<bool> changed{false};
    QObject::connect(
        model.get(),
        &FileListModel::dataChanged,
        model.get(),
        [&changedOn, &changed](const QModelIndex&, const QModelIndex&, const QList<int>&) {
            changedOn.store(QThread::currentThread());
            changed.store(true);
        },
        Qt::DirectConnection);

    WorkerDelivery delivery;
    controller->requestThumbnail(5);
    ASSERT_TRUE(static_cast<bool>(onDone));

    // Act: the fetch completes on a worker
    delivery.deliver([onDone]() {
        onDone(Result<std::string>::ok("C:\\tmp\\5.jpg"));
    });
    delivery.joinAll();

    ASSERT_TRUE(waitFor([&changed]() {
        return changed.load();
    }));
    EXPECT_EQ(changedOn.load(), QThread::currentThread());
}

} // namespace
