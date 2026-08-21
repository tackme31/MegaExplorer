#include "qml/UploadController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "platform/QtLocalFileSystem.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <QDir>
#include <QFile>
#include <QLocale>
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
        client = std::make_shared<MockMegaClient>();
        EXPECT_CALL(*client, checkUpload(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<void>::ok()));
        EXPECT_CALL(*client, findChildFiles(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));
        EXPECT_CALL(*client, findChildFolders(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));

        service = std::make_shared<UploadService>(client);
        // The real local filesystem, not a fake: every test here already makes its
        // files on disk, because dropUrls classifies through QFileInfo.
        scan = std::make_shared<UploadScanService>(client, std::make_shared<QtLocalFileSystem>());
        notifications = std::make_unique<NotificationController>();
        controller = std::make_unique<UploadController>(service, scan, notifications.get());

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
        QObject::connect(controller.get(),
                         &UploadController::uploadRequiresConfirmation,
                         controller.get(),
                         [this](QStringList filePaths, int fileCount, quint64, bool) {
                             ++confirmAsks;
                             askedUploadPaths = filePaths;
                             askedUploadCount = fileCount;
                         });
        QObject::connect(
            controller.get(),
            &UploadController::nameConflictRequiresConfirmation,
            controller.get(),
            [this](QStringList filePaths,
                   QStringList conflictNames,
                   int unaffected,
                   QString conflictingSize,
                   QString unaffectedSize,
                   quint64 handle,
                   bool) {
                ++conflictAsks;
                askedFilePaths = filePaths;
                askedConflictNames = conflictNames;
                askedUnaffected = unaffected;
                askedConflictingSize = conflictingSize;
                askedUnaffectedSize = unaffectedSize;
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

    QString makeFileOfSize(const QString& name, int bytes) const
    {
        QString path = dir.filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(bytes, 'x'));
        file.close();
        return path;
    }

    QString makeDir(const QString& name) const
    {
        QDir(dir.path()).mkdir(name);
        return dir.filePath(name);
    }

    // A directory holding fileCount files, to exercise the recursive cap count.
    QString makeTree(const QString& name, int fileCount) const
    {
        QString path = makeDir(name);
        for (int i = 0; i < fileCount; ++i)
        {
            QFile file(QDir(path).filePath(QStringLiteral("f%1.txt").arg(i)));
            EXPECT_TRUE(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        return path;
    }

    // A drop of more than one file now stops at "upload N files?". Tests below are
    // about what happens *after* that, so they answer it straight away. A no-op when
    // no question was asked -- a single file, or a drop the cap already refused --
    // which is what lets every dropUrls() call route through here.
    void dropAndConfirm(const QList<QUrl>& urls, quint64 target, bool targetIsRoot)
    {
        const int asksBefore = confirmAsks;
        controller->dropUrls(urls, target, targetIsRoot);
        if (confirmAsks > asksBefore)
            controller->uploadConfirmed(askedUploadPaths, target, targetIsRoot);
    }

    QTemporaryDir dir;
    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<UploadService> service;
    std::shared_ptr<UploadScanService> scan;
    std::unique_ptr<NotificationController> notifications;
    std::unique_ptr<UploadController> controller;

    int operationCalls = 0;
    QString lastOperationContext;
    int lastSucceeded = 0;
    int lastFailed = 0;
    int confirmAsks = 0;
    QStringList askedUploadPaths;
    int askedUploadCount = 0;
    int errorCalls = 0;
    QString lastErrorContext;
    NotificationController::ErrorReason lastErrorReason = NotificationController::Unknown;
    QString lastErrorRaw;
    int conflictAsks = 0;
    QStringList askedFilePaths;
    QStringList askedConflictNames;
    int askedUnaffected = 0;
    QString askedConflictingSize;
    QString askedUnaffectedSize;
    quint64 askedHandle = 0;
    int destinationChanges = 0;
    quint64 changedDestination = 0;
};

TEST_F(UploadControllerTest, DropWithoutFoldersEnqueuesEveryFileDirectly)
{
    // Arrange: upload()'s callbacks are never invoked, so the queue stays put.
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(2);

    // Act
    dropAndConfirm(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    // Assert: both queued -- and both started, the queue being shorter than the
    // concurrency limit -- with no question asked
    EXPECT_EQ(conflictAsks, 0);
    EXPECT_EQ(controller->pendingCount(), 2);
}

TEST_F(UploadControllerTest, MoreThanOneFileIsConfirmedBeforeAnythingIsEnqueued)
{
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    EXPECT_EQ(confirmAsks, 1);
    EXPECT_EQ(askedUploadCount, 2);
    EXPECT_EQ(askedUploadPaths.size(), 2);
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, ASingleFileIsNotConfirmed)
{
    // Otherwise every drag of one item onto a folder would cost a click.
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(1);

    controller->dropUrls({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);

    EXPECT_EQ(confirmAsks, 0);
    EXPECT_EQ(controller->pendingCount(), 1);
}

TEST_F(UploadControllerTest, TheConfirmedCountIsWhatIsInsideADroppedFolder)
{
    // One dropped item, three files: the question has to name what actually goes
    // up, which is the same rule the cap counts by.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    controller->dropUrls({QUrl::fromLocalFile(makeTree("sub", 3))}, 7, false);

    EXPECT_EQ(confirmAsks, 1);
    EXPECT_EQ(askedUploadCount, 3);
    EXPECT_EQ(askedUploadPaths.size(), 1); // still one job
}

TEST_F(UploadControllerTest, TheCapRefusesWithoutAskingToConfirm)
{
    // The cap is a refusal, not a question. Stacking the confirmation on top would
    // ask about an upload that is not going to happen either way.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    controller->dropUrls(
        {QUrl::fromLocalFile(makeTree("sub", UploadController::kMaxFilesPerUpload + 1))}, 7, false);

    EXPECT_EQ(confirmAsks, 0);
    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadTooManyFiles"));
}

TEST_F(UploadControllerTest, DecliningTheConfirmationUploadsNothing)
{
    // "Declining" is simply never answering: the dialog closes and the paths are
    // dropped, so nothing in the controller is left holding them.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    EXPECT_EQ(controller->pendingCount(), 0);
    EXPECT_EQ(conflictAsks, 0);
}

TEST_F(UploadControllerTest, ConfirmingStillStopsAtTheSameNameQuestion)
{
    // The two questions are ordered, not exclusive.
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    controller->dropUrls(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);
    ASSERT_EQ(confirmAsks, 1);
    controller->uploadConfirmed(askedUploadPaths, 7, false);

    EXPECT_EQ(conflictAsks, 1);
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, DroppedFolderIsEnqueuedWholeAlongsideLooseFiles)
{
    // One job per dropped item: the SDK walks the tree itself, so the two files
    // inside "sub" never become jobs here.
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(2);

    // Act
    dropAndConfirm(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeTree("sub", 2))},
        7,
        false);

    // Assert
    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(controller->pendingCount(), 2);
}

TEST_F(UploadControllerTest, DropOfAFolderOnlyIsUploaded)
{
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(1);

    // Act
    dropAndConfirm({QUrl::fromLocalFile(makeTree("sub", 3))}, 7, false);

    // Assert
    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(controller->pendingCount(), 1);
}

TEST_F(UploadControllerTest, AFolderIsNeverOfferedAsAReplacementTarget)
{
    // MEGA lets a file and a folder share a name, so a same-named file must not
    // turn a folder upload into "replace that file".
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("sub", 55)})));
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(1);

    // Act
    dropAndConfirm({QUrl::fromLocalFile(makeTree("sub", 1))}, 7, false);

    // Assert: no question, and the folder went straight to the queue
    EXPECT_EQ(conflictAsks, 0);
    EXPECT_EQ(controller->pendingCount(), 1);
}

TEST_F(UploadControllerTest, NonLocalUrlsAreIgnored)
{
    // Act: e.g. an image dragged straight out of a browser
    dropAndConfirm({QUrl(QStringLiteral("https://example.com/a.png"))}, 7, false);

    // Assert
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadNothingToUpload"));
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, UploadOfExactlyTheCapIsStillAccepted)
{
    // The paths never have to exist: the count is checked before collisionsFor,
    // and upload() is mocked out. Only the concurrency limit's worth actually
    // starts; the rest of the cap sits in the queue behind them.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _))
        .Times(static_cast<int>(UploadService::kMaxConcurrent));
    QStringList paths;
    for (int i = 0; i < UploadController::kMaxFilesPerUpload; ++i)
        paths.append(QStringLiteral("C:\\tmp\\f%1.txt").arg(i));

    // Act: the cap lets it through, then the confirmation asks about it -- this
    // test is about the former, so answer the latter.
    controller->uploadFiles(paths, 7, false);
    ASSERT_EQ(confirmAsks, 1);
    controller->uploadConfirmed(paths, 7, false);

    // Assert
    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(controller->pendingCount(), UploadController::kMaxFilesPerUpload);
}

TEST_F(UploadControllerTest, UploadOverTheCapIsRejectedWholesale)
{
    // Rejecting the whole operation, not the tail past the cap: a silently
    // truncated upload is worse than none.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);
    QStringList paths;
    for (int i = 0; i <= UploadController::kMaxFilesPerUpload; ++i)
        paths.append(QStringLiteral("C:\\tmp\\f%1.txt").arg(i));

    // Act
    controller->uploadFiles(paths, 7, false);

    // Assert
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadTooManyFiles"));
    EXPECT_EQ(conflictAsks, 0);
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, SkipKeepsAFolderWhoseNameOnlyCollidedAsAFile)
{
    // The dropped folder and the dropped file share a leaf name, but only the
    // file was ever part of the question -- skipping it must not drop the folder.
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("report", 55)})));
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(1);
    QDir(dir.path()).mkdir(QStringLiteral("other"));
    const QString file = makeFile(QStringLiteral("report"));
    const QString folder = makeTree(QStringLiteral("other/report"), 1);

    // Act
    controller->uploadSkippingExisting({file, folder}, 7, false);

    // Assert: the folder went up, the file did not
    EXPECT_EQ(controller->pendingCount(), 1);
}

TEST_F(UploadControllerTest, TheCapCountsWhatIsInsideADroppedFolder)
{
    // One dropped item, so a cap that counted items would let this through.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    // Act
    dropAndConfirm(
        {QUrl::fromLocalFile(makeTree("sub", UploadController::kMaxFilesPerUpload + 1))}, 7, false);

    // Assert
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadTooManyFiles"));
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, LooseFilesAndAFolderShareOneCap)
{
    // The cap is on the operation, so a folder just under it plus one loose file
    // is over -- neither half is over on its own.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    // Act
    dropAndConfirm(
        {QUrl::fromLocalFile(makeFile("a.txt")),
         QUrl::fromLocalFile(makeTree("sub", UploadController::kMaxFilesPerUpload))},
        7,
        false);

    // Assert
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadTooManyFiles"));
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, EnqueuedPathsUseNativeSeparators)
{
    // The path crosses into the SDK's own LocalPath, which splits on '\' on
    // Windows (Phase 4's gotcha).
    std::string captured;
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).WillOnce(SaveArg<0>(&captured));

    // Act
    dropAndConfirm({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);

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
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    // Act
    dropAndConfirm(
        {QUrl::fromLocalFile(makeFile("a.txt")), QUrl::fromLocalFile(makeFile("b.txt"))}, 7, false);

    // Assert: filePaths carries the whole set (the answer re-derives which is
    // which), conflictNames only the hits
    ASSERT_EQ(conflictAsks, 1);
    EXPECT_EQ(askedFilePaths.size(), 2);
    EXPECT_EQ(askedConflictNames, QStringList{QStringLiteral("a.txt")});
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, ReplaceEnqueuesEverythingAndBinsNothing)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    // Never completing the transfers keeps the whole queue observable.
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(2);
    // The point of the whole change: MEGA folds a same-named upload into the
    // existing node as a new version, so binning that node would throw the version
    // away instead of replacing anything.
    EXPECT_CALL(*client, moveToRubbish(_, _)).Times(0);

    // Act
    controller->uploadReplacingExisting({makeFile("a.txt"), makeFile("b.txt")}, 7, false);

    // Assert
    std::vector<UploadJob> jobs = service->jobs();
    ASSERT_EQ(jobs.size(), 2u);
    EXPECT_EQ(jobs[0].name, "a.txt");
    EXPECT_EQ(jobs[1].name, "b.txt");
}

TEST_F(UploadControllerTest, SkipDropsTheConflictingFilesAndUploadsTheRest)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(1);

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
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    // Act
    controller->uploadSkippingExisting({makeFile("a.txt")}, 7, false);

    // Assert
    EXPECT_EQ(controller->pendingCount(), 0);
    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(operationCalls, 0);
}

TEST_F(UploadControllerTest, AFolderThatOnlyGainsFilesIsNotWorthAQuestion)
{
    // The case the one-dialog rewrite exists for: a local copy of an existing
    // folder with one extra file in it merges to 101 files and asks nothing,
    // because no *file* name is taken (SPEC_NAME_CONFLICT_UPLOAD 3-1).
    EXPECT_CALL(*client, findChildFolders(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("dir", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("other.txt", 55)})));
    EXPECT_CALL(*client, upload(_, 7, false, _, _, _)).Times(1);
    const QString folder = makeDir(QStringLiteral("dir"));
    makeFile(QStringLiteral("dir/file1.txt"));

    // Act
    controller->uploadFiles({folder}, 7, false);

    // Assert: no question, and one folder transfer rather than a file at a time
    EXPECT_EQ(conflictAsks, 0);
    EXPECT_EQ(controller->pendingCount(), 1);
}

TEST_F(UploadControllerTest, ACollisionNestedInADroppedFolderIsAskedAboutByItsPath)
{
    EXPECT_CALL(*client, findChildFolders(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("dir", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);
    const QString folder = makeDir(QStringLiteral("dir"));
    makeFile(QStringLiteral("dir/a.txt"));
    makeFile(QStringLiteral("dir/b.txt"));

    // Act: two files inside, so the count question comes first
    controller->uploadConfirmed({folder}, 7, false);

    // Assert: a bare "a.txt" would not say where it is
    ASSERT_EQ(conflictAsks, 1);
    EXPECT_EQ(askedConflictNames, QStringList{QStringLiteral("dir/a.txt")});
    EXPECT_EQ(askedUnaffected, 1);
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, TheConflictQuestionCarriesTheBytesOnEachSideOfIt)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("big.bin", 55)})));
    EXPECT_CALL(*client, findChildFolders(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);
    const QString colliding = makeFileOfSize(QStringLiteral("big.bin"), 2048);
    const QString other = makeFileOfSize(QStringLiteral("small.bin"), 512);

    // Act
    controller->uploadConfirmed({colliding, other}, 7, false);

    // Assert: the two totals are kept apart, so "Skip" can be priced separately
    // from "Replace". Compared against the same formatter the controller uses --
    // what is under test is which bytes landed in which half, not the units.
    ASSERT_EQ(conflictAsks, 1);
    const QLocale locale = QLocale::c();
    EXPECT_EQ(askedConflictingSize,
              locale.formattedDataSize(2048, 1, QLocale::DataSizeTraditionalFormat));
    EXPECT_EQ(askedUnaffectedSize,
              locale.formattedDataSize(512, 1, QLocale::DataSizeTraditionalFormat));
}

TEST_F(UploadControllerTest, SkipWalksTheCollidingBranchAndSendsTheRestToTheMergedFolder)
{
    EXPECT_CALL(*client, findChildFolders(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("dir", 20)})));
    EXPECT_CALL(*client, findChildFolders(20, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*client, findChildFiles(20, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    const QString folder = makeDir(QStringLiteral("dir"));
    makeFile(QStringLiteral("dir/a.txt"));
    makeFile(QStringLiteral("dir/b.txt"));
    const QString sub = makeTree(QStringLiteral("dir/sub"), 2);

    // Act
    controller->uploadSkippingExisting({folder}, 7, false);

    // Assert: b.txt and the whole untouched subfolder, both into the folder the
    // drop merged with -- not into the drop target, and not a.txt.
    std::vector<UploadJob> jobs = service->jobs();
    ASSERT_EQ(jobs.size(), 2u);
    for (const UploadJob& job : jobs)
    {
        EXPECT_EQ(job.parentHandle, 20u);
        EXPECT_FALSE(job.parentIsRoot);
    }
    EXPECT_EQ(jobs[0].name, "b.txt");
    EXPECT_EQ(jobs[1].name, "sub");
    EXPECT_EQ(QString::fromStdString(jobs[1].localPath), QDir::toNativeSeparators(sub));
}

TEST_F(UploadControllerTest, SkipStopsRatherThanUploadWhatItCannotCheck)
{
    // The opposite call from the question itself: an unanswerable scan there only
    // costs a dialog, but here it would version over the files Skip was chosen to
    // spare.
    EXPECT_CALL(*client, findChildFiles(_, _, _))
        .WillRepeatedly(
            Return(Result<std::vector<FileEntry>>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    // Act
    controller->uploadSkippingExisting({makeFile("a.txt")}, 7, false);

    // Assert
    ASSERT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("uploadSkipFailed"));
    EXPECT_EQ(controller->pendingCount(), 0);
}

TEST_F(UploadControllerTest, BatchReportsOnceAndAnnouncesTheDestinationAfterTheQueueDrains)
{
    EXPECT_CALL(*client, upload(_, _, _, _, _, _))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{1})));

    // Act
    dropAndConfirm(
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
    EXPECT_CALL(*client, upload(_, _, _, _, _, _)).Times(0);

    // Act
    dropAndConfirm({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);
    flushQueuedEvents();

    // Assert: no destinationChanged -- nothing about that folder changed
    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastOperationContext, QStringLiteral("uploadDestinationGone"));
    EXPECT_EQ(lastFailed, 1);
    EXPECT_EQ(destinationChanges, 0);
}

TEST_F(UploadControllerTest, IsUploadingToCoversTheDestinationFromEnqueueUntilTheQueueDrains)
{
    EXPECT_CALL(*client, upload(_, _, _, _, _, _))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{1})));

    // Act: the finished notifications are still queued at this point
    dropAndConfirm(
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

    dropAndConfirm({QUrl::fromLocalFile(makeFile("a.txt"))}, 7, false);
    flushQueuedEvents();

    EXPECT_FALSE(controller->isUploadingTo(7, false));
}

TEST_F(UploadControllerTest, ReplaceStopsSpinningAsSoonAsTheUploadLands)
{
    EXPECT_CALL(*client, findChildFiles(7, false, _))
        .WillRepeatedly(Return(Result<std::vector<FileEntry>>::ok({entry("a.txt", 55)})));
    EXPECT_CALL(*client, upload(_, _, _, _, _, _))
        .WillRepeatedly(::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{1})));

    controller->uploadReplacingExisting({makeFile("a.txt")}, 7, false);
    flushQueuedEvents();

    // Nothing follows the transfer any more, so the destination must not keep a
    // spinner waiting on a second step that no longer exists.
    EXPECT_FALSE(controller->isUploadingTo(7, false));
}

} // namespace
