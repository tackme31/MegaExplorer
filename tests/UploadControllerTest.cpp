#include "qml/UploadController.h"

#include "core/FileOperationService.h"
#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Signals are observed with plain QObject::connect lambdas rather than
// QSignalSpy, which lives in Qt6::Test and would be a new dependency for this
// target; same convention as TabsControllerTest/QuickAccessModelTest.
//
// TRAP: Result<void>::success defaults to false (src/core/Result.h), so gmock's
// default action for an unstubbed checkUpload() is *failure*, which makes
// UploadService fail every job before it reaches upload(). Hence the blanket
// expectations in SetUp().
namespace
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::SaveArg;

FileEntry entry(const char* name, std::uint64_t handle)
{
    FileEntry e;
    e.name = name;
    e.handle = handle;
    return e;
}

class UploadControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testApp();
        client = std::make_shared<MockMegaClient>();
        EXPECT_CALL(*client, checkUpload(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<void>::ok()));
        EXPECT_CALL(*client, findChildFiles(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));

        service = std::make_shared<UploadService>(client);
        fileOps = std::make_shared<FileOperationService>(client);
        notifications = std::make_unique<NotificationController>();
        controller = std::make_unique<UploadController>(service, fileOps, notifications.get());

        QObject::connect(notifications.get(),
                         &NotificationController::operationFinished,
                         notifications.get(),
                         [this](QString context, int succeeded, int failed) {
                             ++operationCalls;
                             lastOperationContext = context;
                             lastSucceeded = succeeded;
                             lastFailed = failed;
                         });
        QObject::connect(notifications.get(),
                         &NotificationController::errorOccurred,
                         notifications.get(),
                         [this](QString context,
                                NotificationController::ErrorReason reason,
                                QString rawMessage) {
                             ++errorCalls;
                             lastErrorContext = context;
                             lastErrorReason = reason;
                             lastErrorRaw = rawMessage;
                         });
        QObject::connect(
            controller.get(),
            &UploadController::folderDropRequiresConfirmation,
            controller.get(),
            [this](QStringList filePaths, int folderCount, quint64 handle, bool isRoot) {
                ++folderAsks;
                askedFilePaths = filePaths;
                askedFolderCount = folderCount;
                askedHandle = handle;
                askedIsRoot = isRoot;
            });
        QObject::connect(
            controller.get(),
            &UploadController::nameConflictRequiresConfirmation,
            controller.get(),
            [this](QStringList filePaths, QStringList conflictNames, quint64 handle, bool) {
                ++conflictAsks;
                askedFilePaths = filePaths;
                askedConflictNames = conflictNames;
                askedHandle = handle;
            });
        QObject::connect(controller.get(),
                         &UploadController::destinationChanged,
                         controller.get(),
                         [this](quint64 handle, bool) {
                             ++destinationChanges;
                             changedDestination = handle;
                         });
    }

    // An actual file on disk: dropUrls classifies through QFileInfo, so these
    // have to exist for real.
    QString makeFile(const QString& name) const
    {
        QString path = dir.filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
        return path;
    }

    QString makeDir(const QString& name) const
    {
        QDir(dir.path()).mkdir(name);
        return dir.filePath(name);
    }

    QTemporaryDir dir;
    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<UploadService> service;
    std::shared_ptr<FileOperationService> fileOps;
    std::unique_ptr<NotificationController> notifications;
    std::unique_ptr<UploadController> controller;

    int operationCalls = 0;
    QString lastOperationContext;
    int lastSucceeded = 0;
    int lastFailed = 0;
    int errorCalls = 0;
    QString lastErrorContext;
    NotificationController::ErrorReason lastErrorReason = NotificationController::Unknown;
    QString lastErrorRaw;
    int folderAsks = 0;
    int conflictAsks = 0;
    QStringList askedFilePaths;
    QStringList askedConflictNames;
    int askedFolderCount = 0;
    quint64 askedHandle = 0;
    bool askedIsRoot = false;
    int destinationChanges = 0;
    quint64 changedDestination = 0;
};

TEST_F(UploadControllerTest, DropWithoutFoldersEnqueuesEveryFileDirectly)
{
    // Arrange: upload()'s callbacks are never invoked, so the queue stays put.
    EXPECT_CALL(*client, upload(_, 7, false, _, _)).Times(1);

    // Act
    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    // Assert: both queued (only the first started), no question asked
    EXPECT_EQ(folderAsks, 0);
    EXPECT_EQ(conflictAsks, 0);
    EXPECT_EQ(controller->pendingCount(), 2);
}

TEST_F(UploadControllerTest, DropContainingAFolderAsksBeforeEnqueueingAnything)
{
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(0);

    // Act
    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeDir("sub"))}, 7, false);

    // Assert: the destination rides along so it outlives the dialog
    ASSERT_EQ(folderAsks, 1);
    EXPECT_EQ(askedFilePaths.size(), 1);
    EXPECT_EQ(askedFolderCount, 1);
    EXPECT_EQ(askedHandle, 7u);
    EXPECT_FALSE(askedIsRoot);
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, DropOfFoldersOnlyReportsNothingToUploadInsteadOfAsking)
{
    // "Upload the remaining 0 file(s)?" would be a terrible question, so the
    // empty check runs before the folder one.

    // Act
    controller->dropUrls({QUrl::fromLocalFile(makeDir("sub"))}, 7, false);

    // Assert
    EXPECT_EQ(folderAsks, 0);
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadNothingToUpload"));
}

TEST_F(UploadControllerTest, NonLocalUrlsAreIgnored)
{
    // Act: e.g. an image dragged straight out of a browser
    controller->dropUrls({QUrl(QStringLiteral("https://example.com/a.png"))}, 7, false);

    // Assert
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadNothingToUpload"));
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, EnqueuedPathsUseNativeSeparators)
{
    // The path crosses into the SDK's own LocalPath, which splits on '\' on
    // Windows (Phase 4's gotcha).
    std::string captured;
    EXPECT_CALL(*client, upload(_, _, _, _, _)).WillOnce(SaveArg<0>(&captured));

    // Act
    controller->dropUrls({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);

    // Assert
    ASSERT_FALSE(captured.empty());
    EXPECT_EQ(QString::fromStdString(captured), QDir::toNativeSeparators(dir.filePath("a.txt")));
}

TEST_F(UploadControllerTest, CanUploadToReducesTheResultToABool)
{
    EXPECT_CALL(*client, checkUpload(9, false))
        .WillRepeatedly(Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));

    EXPECT_TRUE(controller->canUploadTo(7, false));
    EXPECT_FALSE(controller->canUploadTo(9, false));
}

TEST_F(UploadControllerTest, NameCollisionAsksBeforeEnqueueingAnything)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(0);

    // Act
    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    // Assert: filePaths carries the whole set (the answer re-derives which is
    // which), conflictNames only the hits
    ASSERT_EQ(conflictAsks, 1);
    EXPECT_EQ(askedFilePaths.size(), 2);
    EXPECT_EQ(askedConflictNames, QStringList{QStringLiteral("a.txt")});
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, ReplaceEnqueuesEverythingAndTagsOnlyTheConflictingFile)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    // Never completing the transfers keeps the whole queue observable.
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(1);

    // Act
    controller->uploadReplacingExisting({makeFile("a.txt"), makeFile("b.txt")}, 7, false);

    // Assert
    std::vector<UploadJob> jobs = service->jobs();
    ASSERT_EQ(jobs.size(), 2u);
    EXPECT_EQ(jobs[0].name, "a.txt");
    EXPECT_EQ(jobs[0].replaceHandle, 55u);
    EXPECT_EQ(jobs[1].name, "b.txt");
    EXPECT_EQ(jobs[1].replaceHandle, 0u);
}

TEST_F(UploadControllerTest, ReplaceHandleGoesToTheFirstOfTwoSameNamedFilesOnly)
{
    // Two dropped files can share a leaf name while there's only one node to
    // replace -- binning it twice would just fail the second time with kENoEnt.
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(
            Return(Result<std::vector<FileEntry>>::ok({entry("x.txt", 55), entry("x.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(1);

    QDir(dir.path()).mkdir("one");
    QDir(dir.path()).mkdir("two");

    // Act
    controller->uploadReplacingExisting({makeFile("one/x.txt"), makeFile("two/x.txt")}, 7, false);

    // Assert
    std::vector<UploadJob> jobs = service->jobs();
    ASSERT_EQ(jobs.size(), 2u);
    EXPECT_EQ(jobs[0].replaceHandle, 55u);
    EXPECT_EQ(jobs[1].replaceHandle, 0u);
}

TEST_F(UploadControllerTest, SkipDropsTheConflictingFilesAndUploadsTheRest)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(1);

    // Act
    controller->uploadSkippingExisting({makeFile("a.txt"), makeFile("b.txt")}, 7, false);

    // Assert
    std::vector<UploadJob> jobs = service->jobs();
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].name, "b.txt");
}

TEST_F(UploadControllerTest, SkippingEverythingUploadsNothingAndSaysNothing)
{
    // Silence is right: an empty result is exactly what the user asked for.
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(0);

    // Act
    controller->uploadSkippingExisting({makeFile("a.txt")}, 7, false);

    // Assert
    EXPECT_EQ(controller->pendingCount(), 0);
    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(operationCalls, 0);
}

TEST_F(UploadControllerTest, BatchReportsOnceAndAnnouncesTheDestinationAfterTheQueueDrains)
{
    EXPECT_CALL(*client, upload(_, _, _, _, _))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{1})));

    // Act
    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);
    flushQueuedEvents(); // the finished notifications are queued onto the GUI thread

    // Assert: two files, one snackbar, one refresh request
    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastOperationContext, QStringLiteral("upload"));
    EXPECT_EQ(lastSucceeded, 2);
    EXPECT_EQ(lastFailed, 0);
    ASSERT_EQ(destinationChanges, 1);
    EXPECT_EQ(changedDestination, 7u);
}

TEST_F(UploadControllerTest, WholeBatchLostToAMissingDestinationGetsItsOwnMessage)
{
    EXPECT_CALL(*client, checkUpload(7, false))
        .WillRepeatedly(Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, upload(_, _, _, _, _)).Times(0);

    // Act
    controller->dropUrls({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);
    flushQueuedEvents();

    // Assert: no destinationChanged -- nothing about that folder changed
    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastOperationContext, QStringLiteral("uploadDestinationGone"));
    EXPECT_EQ(lastFailed, 1);
    EXPECT_EQ(destinationChanges, 0);
}

TEST_F(UploadControllerTest, IsUploadingToCoversTheDestinationFromEnqueueUntilTheQueueDrains)
{
    EXPECT_CALL(*client, upload(_, _, _, _, _))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{1})));

    // Act: the finished notifications are still queued at this point
    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    // Assert: busy for this destination only
    EXPECT_TRUE(controller->isUploadingTo(7, false));
    EXPECT_FALSE(controller->isUploadingTo(7, true));
    EXPECT_FALSE(controller->isUploadingTo(9, false));

    flushQueuedEvents();
    EXPECT_FALSE(controller->isUploadingTo(7, false));
}

TEST_F(UploadControllerTest, IsUploadingToClearsEvenWhenEveryJobFails)
{
    EXPECT_CALL(*client, checkUpload(7, false))
        .WillRepeatedly(Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));

    controller->dropUrls({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);
    flushQueuedEvents();

    EXPECT_FALSE(controller->isUploadingTo(7, false));
}

TEST_F(UploadControllerTest, IsUploadingToOutlastsTheUploadWhileTheReplacedNodeIsStillBeingBinned)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _))
        .WillRepeatedly(::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{1})));
    // Never answered, so the replace stays in flight.
    EXPECT_CALL(*client, moveToRubbish(55, _)).Times(1);

    controller->uploadReplacingExisting({makeFile("a.txt")}, 7, false);
    flushQueuedEvents();

    EXPECT_TRUE(controller->isUploadingTo(7, false));
}

} // namespace
