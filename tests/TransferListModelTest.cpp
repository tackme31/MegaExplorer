#include "qml/TransferListModel.h"

#include "core/DownloadService.h"
#include "core/MegaErrorCodes.h"
#include "core/UploadScanService.h"
#include "core/UploadService.h"
#include "MockMegaClient.h"
#include "platform/QtLocalFileSystem.h"
#include "qml/DownloadController.h"
#include "qml/NotificationController.h"
#include "qml/TransferEnums.h"
#include "qml/UploadController.h"
#include "TestApp.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

// Drives the real controllers rather than poking the model directly: the wiring
// (which callback publishes which job, and on which thread it lands) is half of
// what this class is.
namespace
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::Return;

using ProgressCallback = std::function<void(std::uint64_t, std::uint64_t)>;
using DownloadDone = std::function<void(Result<DownloadOutcome>)>;
using UploadDone = std::function<void(Result<UploadOutcome>)>;

// Same two-drain reason as DownloadControllerTest's: the completion path posts a
// second time after emitting.
void flush()
{
    flushQueuedEvents();
    flushQueuedEvents();
}

class TransferListModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        client = std::make_shared<MockMegaClient>();
        EXPECT_CALL(*client, download(_, _, _, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](std::uint64_t,
                                          const std::string&,
                                          std::uint64_t,
                                          ProgressCallback progress,
                                          DownloadDone done) {
                downloadProgress = std::move(progress);
                downloadDone = done;
                downloadDones.push_back(std::move(done));
            }));
        EXPECT_CALL(*client, upload(_, _, _, _, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string&,
                                          std::uint64_t,
                                          bool,
                                          std::uint64_t,
                                          ProgressCallback progress,
                                          UploadDone done) {
                uploadProgress = std::move(progress);
                uploadDone = std::move(done);
            }));
        EXPECT_CALL(*client, checkUpload(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<void>::ok()));
        EXPECT_CALL(*client, findChildFiles(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));
        EXPECT_CALL(*client, findChildFolders(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));
        EXPECT_CALL(*client, cancelDownload(_)).Times(AnyNumber());
        EXPECT_CALL(*client, cancelUpload(_)).Times(AnyNumber());

        downloadService = std::make_shared<DownloadService>(client);
        uploadService = std::make_shared<UploadService>(client);
        scan = std::make_shared<UploadScanService>(client, std::make_shared<QtLocalFileSystem>());
        notifications = std::make_unique<NotificationController>();
        downloads = std::make_unique<DownloadController>(downloadService, notifications.get());
        uploads = std::make_unique<UploadController>(uploadService, scan, notifications.get());
        model = std::make_unique<TransferListModel>(downloads.get(), uploads.get());

        QObject::connect(model.get(), &TransferListModel::countChanged, model.get(), [this] {
            ++countChanges;
        });
    }

    // One real file on disk, because UploadController classifies drops through
    // QFileInfo. A single path also stays under the "more than one file" gate, so
    // no confirmation dialog stands between the call and the queue.
    QString makeLocalFile(const QString& name)
    {
        QFile f(tempDir.filePath(name));
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        return QDir::toNativeSeparators(tempDir.filePath(name));
    }

    QVariant role(int row, int r) const
    {
        return model->data(model->index(row), r);
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<DownloadService> downloadService;
    std::shared_ptr<UploadService> uploadService;
    std::shared_ptr<UploadScanService> scan;
    std::unique_ptr<NotificationController> notifications;
    std::unique_ptr<DownloadController> downloads;
    std::unique_ptr<UploadController> uploads;
    std::unique_ptr<TransferListModel> model;

    QTemporaryDir tempDir;
    ProgressCallback downloadProgress;
    DownloadDone downloadDone;               // the most recently started transfer's
    std::vector<DownloadDone> downloadDones; // all of them, in start order
    ProgressCallback uploadProgress;
    UploadDone uploadDone;
    int countChanges = 0;
};

TEST_F(TransferListModelTest, StartsEmpty)
{
    EXPECT_EQ(model->rowCount(), 0);
    EXPECT_EQ(model->count(), 0);
}

TEST_F(TransferListModelTest, EnqueuedDownloadsAppearImmediately)
{
    downloads->downloadFile(7, "a.txt", 100);
    downloads->downloadFile(8, "b.txt", 200);

    ASSERT_EQ(model->rowCount(), 2);
    EXPECT_EQ(role(0, TransferListModel::DirectionRole).toInt(), TransferDirectionEnum::Download);
    EXPECT_EQ(role(0, TransferListModel::NameRole).toString(), QStringLiteral("a.txt"));
    EXPECT_EQ(role(0, TransferListModel::StateRole).toInt(), TransferStateEnum::Active);
    // Both are running: two is under the service's concurrency limit.
    EXPECT_EQ(role(1, TransferListModel::NameRole).toString(), QStringLiteral("b.txt"));
    EXPECT_EQ(role(1, TransferListModel::StateRole).toInt(), TransferStateEnum::Active);
    EXPECT_EQ(countChanges, 2);
}

TEST_F(TransferListModelTest, ProgressMovesTheRowItBelongsTo)
{
    downloads->downloadFile(7, "a.txt", 100);
    ASSERT_TRUE(static_cast<bool>(downloadProgress));
    downloadProgress(40, 100);
    flush();

    EXPECT_DOUBLE_EQ(role(0, TransferListModel::ProgressRole).toDouble(), 0.4);
    EXPECT_EQ(role(0, TransferListModel::TransferredBytesRole).toULongLong(), 40u);
    EXPECT_EQ(role(0, TransferListModel::TotalBytesRole).toULongLong(), 100u);
    EXPECT_EQ(countChanges, 1); // an update, not an append
}

TEST_F(TransferListModelTest, FinishedDownloadsStayInTheList)
{
    downloads->downloadFile(7, "a.txt", 100);
    ASSERT_TRUE(static_cast<bool>(downloadDone));
    DownloadDone done = downloadDone;
    done(Result<DownloadOutcome>::ok(DownloadOutcome{"C:/dl/a.txt"}));
    flush();

    ASSERT_EQ(model->rowCount(), 1);
    EXPECT_EQ(role(0, TransferListModel::StateRole).toInt(), TransferStateEnum::Completed);
    EXPECT_DOUBLE_EQ(role(0, TransferListModel::ProgressRole).toDouble(), 1.0);
}

TEST_F(TransferListModelTest, AFailedDownloadKeepsItsRowAndItsRequestedName)
{
    downloads->downloadFile(7, "a.txt", 100);
    DownloadDone done = downloadDone;
    done(Result<DownloadOutcome>::fail("network error", 2));
    flush();

    ASSERT_EQ(model->rowCount(), 1);
    EXPECT_EQ(role(0, TransferListModel::StateRole).toInt(), TransferStateEnum::Failed);
    EXPECT_EQ(role(0, TransferListModel::NameRole).toString(), QStringLiteral("a.txt"));
    EXPECT_DOUBLE_EQ(role(0, TransferListModel::ProgressRole).toDouble(), 0.0);
}

TEST_F(TransferListModelTest, CancelledDownloadsKeepTheirRows)
{
    downloads->downloadFile(7, "a.txt", 100);
    downloads->downloadFile(8, "b.txt", 200);
    downloads->cancelDownloads();
    // Both are running, so neither settles until the SDK acknowledges its abort.
    ASSERT_EQ(downloadDones.size(), 2u);
    downloadDones[1](
        Result<DownloadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));
    flush();

    ASSERT_EQ(model->rowCount(), 2);
    EXPECT_EQ(role(0, TransferListModel::StateRole).toInt(), TransferStateEnum::Active);
    EXPECT_EQ(role(1, TransferListModel::StateRole).toInt(), TransferStateEnum::Cancelled);
}

TEST_F(TransferListModelTest, DownloadsAndUploadsShareOneListInEnqueueOrder)
{
    ASSERT_TRUE(tempDir.isValid());
    downloads->downloadFile(7, "a.txt", 100);
    uploads->uploadFiles({makeLocalFile(QStringLiteral("up.txt"))}, 5, false);
    downloads->downloadFile(8, "b.txt", 200);
    flush();

    ASSERT_EQ(model->rowCount(), 3);
    EXPECT_EQ(role(0, TransferListModel::NameRole).toString(), QStringLiteral("a.txt"));
    EXPECT_EQ(role(1, TransferListModel::NameRole).toString(), QStringLiteral("up.txt"));
    EXPECT_EQ(role(1, TransferListModel::DirectionRole).toInt(), TransferDirectionEnum::Upload);
    EXPECT_EQ(role(2, TransferListModel::NameRole).toString(), QStringLiteral("b.txt"));
}

// Job ids restart at 1 in each service, so keying rows by id alone would make the
// first upload overwrite the first download.
TEST_F(TransferListModelTest, TheFirstUploadDoesNotOverwriteTheFirstDownload)
{
    ASSERT_TRUE(tempDir.isValid());
    downloads->downloadFile(7, "a.txt", 100);
    uploads->uploadFiles({makeLocalFile(QStringLiteral("up.txt"))}, 5, false);
    flush();

    ASSERT_EQ(model->rowCount(), 2);
    EXPECT_EQ(role(0, TransferListModel::DirectionRole).toInt(), TransferDirectionEnum::Download);
    EXPECT_EQ(role(1, TransferListModel::DirectionRole).toInt(), TransferDirectionEnum::Upload);
}

TEST_F(TransferListModelTest, UploadProgressAndCompletionReachTheRow)
{
    ASSERT_TRUE(tempDir.isValid());
    uploads->uploadFiles({makeLocalFile(QStringLiteral("up.txt"))}, 5, false);
    flush();
    ASSERT_EQ(model->rowCount(), 1);
    ASSERT_TRUE(static_cast<bool>(uploadProgress));

    uploadProgress(30, 60);
    flush();
    EXPECT_DOUBLE_EQ(role(0, TransferListModel::ProgressRole).toDouble(), 0.5);

    UploadDone done = uploadDone;
    done(Result<UploadOutcome>::ok(UploadOutcome{1}));
    flush();
    EXPECT_EQ(role(0, TransferListModel::StateRole).toInt(), TransferStateEnum::Completed);
    // Nothing to open: an upload's source is the user's own file, not something
    // this app put there. The flyout keys its clickable name off this being empty.
    EXPECT_TRUE(role(0, TransferListModel::LocalPathRole).toString().isEmpty());
}

// The run summary is what the minimized transfer flyout reads: "9 / 41" plus one
// bar for both directions.
TEST_F(TransferListModelTest, ARunCoversEveryJobStillInFlight)
{
    EXPECT_FALSE(model->runActive());
    EXPECT_EQ(model->runTotal(), 0);

    downloads->downloadFile(7, "a.txt", 100);
    downloads->downloadFile(8, "b.txt", 200);

    EXPECT_TRUE(model->runActive());
    EXPECT_EQ(model->runTotal(), 2);
    EXPECT_EQ(model->runFinished(), 0);
}

TEST_F(TransferListModelTest, RunProgressAveragesTheRowsInTheRun)
{
    downloads->downloadFile(7, "a.txt", 100);
    downloads->downloadFile(8, "b.txt", 200);
    ASSERT_TRUE(static_cast<bool>(downloadProgress));
    downloadProgress(40, 100);
    flush();

    // 0.4 for the active row, 0.0 for the queued one.
    EXPECT_DOUBLE_EQ(model->runProgress(), 0.2);
}

TEST_F(TransferListModelTest, ASettledRunIsNotCountedIntoTheNextOne)
{
    downloads->downloadFile(7, "a.txt", 100);
    ASSERT_TRUE(static_cast<bool>(downloadDone));
    DownloadDone done = downloadDone;
    done(Result<DownloadOutcome>::ok(DownloadOutcome{"C:/dl/a.txt"}));
    flush();

    EXPECT_FALSE(model->runActive());
    EXPECT_EQ(model->runTotal(), 1);
    EXPECT_EQ(model->runFinished(), 1);
    EXPECT_DOUBLE_EQ(model->runProgress(), 1.0);

    // The finished row stays in the list, but the next burst starts its own run --
    // otherwise the denominator would only ever grow.
    downloads->downloadFile(8, "b.txt", 200);
    EXPECT_EQ(model->rowCount(), 2);
    EXPECT_TRUE(model->runActive());
    EXPECT_EQ(model->runTotal(), 1);
    EXPECT_EQ(model->runFinished(), 0);
}

// A cancelled or failed job settles the run just as a completed one does: the
// flyout has to be able to take itself away.
TEST_F(TransferListModelTest, CancelledAndFailedRowsSettleTheRun)
{
    downloads->downloadFile(7, "a.txt", 100);
    downloads->downloadFile(8, "b.txt", 200);
    downloads->cancelDownloads();
    ASSERT_EQ(downloadDones.size(), 2u);
    downloadDones[0](
        Result<DownloadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));
    downloadDones[1](Result<DownloadOutcome>::fail("network error", 2));
    flush();

    EXPECT_EQ(model->runTotal(), 2);
    EXPECT_EQ(model->runFinished(), 2);
    EXPECT_FALSE(model->runActive());
}

TEST_F(TransferListModelTest, RoleNamesAreTheOnesQmlBindsTo)
{
    const QHash<int, QByteArray> names = model->roleNames();
    EXPECT_EQ(names.value(TransferListModel::DirectionRole), QByteArray("direction"));
    EXPECT_EQ(names.value(TransferListModel::JobIdRole), QByteArray("jobId"));
    EXPECT_EQ(names.value(TransferListModel::NameRole), QByteArray("name"));
    EXPECT_EQ(names.value(TransferListModel::StateRole), QByteArray("state"));
    EXPECT_EQ(names.value(TransferListModel::ProgressRole), QByteArray("progress"));
    EXPECT_EQ(names.value(TransferListModel::TransferredBytesRole), QByteArray("transferredBytes"));
    EXPECT_EQ(names.value(TransferListModel::TotalBytesRole), QByteArray("totalBytes"));
    EXPECT_EQ(names.value(TransferListModel::LocalPathRole), QByteArray("localPath"));
}

// What the flyout's clickable name needs: the row has to name the file that was
// actually written, which is not what NameRole says once a collision renamed it.
TEST_F(TransferListModelTest, ACompletedDownloadCarriesThePathItWasSavedTo)
{
    downloads->downloadFile(7, "a.txt", 100);
    ASSERT_TRUE(static_cast<bool>(downloadDone));
    DownloadDone done = downloadDone;
    EXPECT_TRUE(role(0, TransferListModel::LocalPathRole).toString().isEmpty());

    done(Result<DownloadOutcome>::ok(DownloadOutcome{"C:/dl/a (1).txt"}));
    flush();

    EXPECT_EQ(role(0, TransferListModel::NameRole).toString(), QStringLiteral("a.txt"));
    EXPECT_EQ(role(0, TransferListModel::LocalPathRole).toString(),
              QStringLiteral("C:/dl/a (1).txt"));
}

TEST_F(TransferListModelTest, AFailedDownloadHasNoPathToOpen)
{
    downloads->downloadFile(7, "a.txt", 100);
    DownloadDone done = downloadDone;
    done(Result<DownloadOutcome>::fail("network error", 2));
    flush();

    EXPECT_TRUE(role(0, TransferListModel::LocalPathRole).toString().isEmpty());
}

TEST_F(TransferListModelTest, TheJobIdRoleNamesTheOneRowACancelStops)
{
    // The cancelled row has to be a waiting one: a running row only settles once
    // the SDK acknowledges its abort, so the slots are filled first.
    const int waitingRow = static_cast<int>(DownloadService::kMaxConcurrent);
    for (quint64 handle = 1; handle <= DownloadService::kMaxConcurrent + 2; ++handle)
        downloads->downloadFile(handle, QStringLiteral("f%1.txt").arg(handle), 100);

    // Act: through the same call the flyout's row button makes
    downloads->cancelJob(role(waitingRow, TransferListModel::JobIdRole).toULongLong());
    flush();

    ASSERT_EQ(model->rowCount(), waitingRow + 2);
    EXPECT_EQ(role(0, TransferListModel::StateRole).toInt(), TransferStateEnum::Active);
    EXPECT_EQ(role(waitingRow, TransferListModel::StateRole).toInt(), TransferStateEnum::Cancelled);
    EXPECT_EQ(role(waitingRow + 1, TransferListModel::StateRole).toInt(),
              TransferStateEnum::Queued);
}

} // namespace
