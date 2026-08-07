#include "core/UploadService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

// TRAP: Result<void>::success defaults to false (src/core/Result.h), so gmock's
// default action for an unstubbed checkUpload() returns *failure*. Without the
// blanket expectation below, startNextIfIdle() fails every job before it ever
// reaches upload() -- which shows up as "every test fails", not as a compile
// error. Tests needing the opposite declare their own, later, EXPECT_CALL;
// gmock matches expectations newest-first, so theirs wins.
namespace
{

std::shared_ptr<MockMegaClient> makeClient()
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, checkUpload(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(Result<void>::ok()));
    return mockClient;
}

} // namespace

TEST(UploadServiceTest, InitiallyHasNoCurrentJob)
{
    // Arrange
    auto mockClient = makeClient();
    UploadService service(mockClient);

    // Assert
    EXPECT_FALSE(service.currentJob().has_value());
    EXPECT_TRUE(service.jobs().empty());
    EXPECT_EQ(service.queueLength(), 0u);
}

TEST(UploadServiceTest, EnqueueSuccessNotifiesJobFinishedWithCompletedStateAndNodeHandle)
{
    // Arrange
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient,
                upload(std::string("C:\\tmp\\a.txt"), 7, false, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{42})));

    UploadService service(mockClient);
    bool finishedCalled = false;
    UploadJob finished;
    service.setOnJobFinished([&](UploadJob job) {
        finishedCalled = true;
        finished = std::move(job);
    });

    // Act
    std::uint64_t id = service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 100);

    // Assert
    ASSERT_TRUE(finishedCalled);
    EXPECT_EQ(finished.id, id);
    EXPECT_EQ(finished.state, UploadState::Completed);
    EXPECT_EQ(finished.nodeHandle, 42u);
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(UploadServiceTest, EnqueueFailurePropagatesErrorMessageCodeAndState)
{
    // Arrange
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(Result<UploadOutcome>::fail("network error", 2)));

    UploadService service(mockClient);
    UploadJob finished;
    bool finishedCalled = false;
    service.setOnJobFinished([&](UploadJob job) {
        finishedCalled = true;
        finished = std::move(job);
    });

    // Act
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 100);

    // Assert
    ASSERT_TRUE(finishedCalled);
    EXPECT_EQ(finished.state, UploadState::Failed);
    EXPECT_EQ(finished.errorMessage, "network error");
    EXPECT_EQ(finished.errorCode, 2);
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(UploadServiceTest, EnqueueSeedsTotalBytesFromExpectedTotalBytesBeforeFirstProgressTick)
{
    // Arrange: mocked upload() doesn't invoke either callback, so this observes
    // state exactly as enqueue() left it.
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_));

    UploadService service(mockClient);

    // Act
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 12345);

    // Assert
    std::optional<UploadJob> job = service.currentJob();
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->totalBytes, 12345u);
}

TEST(UploadServiceTest, ProgressCallbackOverwritesSeededTotalBytes)
{
    // Arrange
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::InvokeArgument<3>(std::uint64_t{50}, std::uint64_t{200}),
            ::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{1}))));

    UploadService service(mockClient);
    std::vector<UploadJob> progressSnapshots;
    service.setOnProgress([&](UploadJob job) {
        progressSnapshots.push_back(std::move(job));
    });

    // Act: seeded with 100, but the real transfer reports 200
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 100);

    // Assert
    ASSERT_EQ(progressSnapshots.size(), 1u);
    EXPECT_EQ(progressSnapshots[0].transferredBytes, 50u);
    EXPECT_EQ(progressSnapshots[0].totalBytes, 200u);
}

TEST(UploadServiceTest, SecondEnqueueWhileFirstActiveDoesNotStartImmediatelyThenAutoStarts)
{
    // Arrange: capture both calls' completion callbacks instead of invoking
    // them, so we control exactly when each transfer "finishes".
    auto mockClient = makeClient();
    std::function<void(Result<UploadOutcome>)> onDone1;
    std::function<void(Result<UploadOutcome>)> onDone2;

    EXPECT_CALL(*mockClient,
                upload(std::string("a"), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone1));
    EXPECT_CALL(*mockClient,
                upload(std::string("b"), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone2));

    UploadService service(mockClient);

    // Act
    std::uint64_t id1 = service.enqueue("a", "a.txt", 7, false, 0);
    std::uint64_t id2 = service.enqueue("b", "b.txt", 7, false, 0);

    // Assert: only job 1 started
    ASSERT_FALSE(static_cast<bool>(onDone2));
    auto jobsBefore = service.jobs();
    ASSERT_EQ(jobsBefore.size(), 2u);
    EXPECT_EQ(jobsBefore[0].id, id1);
    EXPECT_EQ(jobsBefore[0].state, UploadState::Active);
    EXPECT_EQ(jobsBefore[1].id, id2);
    EXPECT_EQ(jobsBefore[1].state, UploadState::Queued);

    // Act: finish job 1
    onDone1(Result<UploadOutcome>::ok(UploadOutcome{1}));

    // Assert: job 2 auto-started
    ASSERT_TRUE(static_cast<bool>(onDone2));
    auto jobsAfter = service.jobs();
    ASSERT_EQ(jobsAfter.size(), 1u);
    EXPECT_EQ(jobsAfter[0].id, id2);
    EXPECT_EQ(jobsAfter[0].state, UploadState::Active);
}

TEST(UploadServiceTest, DestinationGoneAtStartTimeFailsWithoutCallingUpload)
{
    // Arrange: hover-time validation passed, but the folder is gone by the
    // time the job's turn comes.
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(7, false))
        .WillRepeatedly(::testing::Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    UploadService service(mockClient);
    UploadJob finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished = std::move(job);
    });

    // Act
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 0);

    // Assert
    EXPECT_EQ(finished.state, UploadState::Failed);
    EXPECT_EQ(finished.errorCode, MegaErrorCode::kENoEnt);
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(UploadServiceTest, DestinationGoneDrainsEveryQueuedJobForThatDestination)
{
    // Regression guard for startNextIfIdle()'s for(;;) loop: a synchronous
    // fast-fail must advance the whole queue without recursing once per job.
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(7, false))
        .WillRepeatedly(::testing::Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    UploadService service(mockClient);
    int finishedCount = 0;
    service.setOnJobFinished([&](UploadJob) {
        ++finishedCount;
    });

    // Act: the first enqueue drains itself; each later one drains on arrival
    for (int i = 0; i < 50; ++i)
        service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 0);

    // Assert
    EXPECT_EQ(finishedCount, 50);
    EXPECT_EQ(service.queueLength(), 0u);
}

TEST(UploadServiceTest, ReplaceHandleRidesAlongUntouchedToTheFinishedNotification)
{
    // UploadService must not act on replaceHandle in any way -- it only
    // carries it (UploadController does the Rubbish-bin move).
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{99})));
    EXPECT_CALL(*mockClient, moveToRubbish(::testing::_, ::testing::_)).Times(0);

    UploadService service(mockClient);
    UploadJob finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished = std::move(job);
    });

    // Act
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 0, /*replaceHandle*/ 1234);

    // Assert
    EXPECT_EQ(finished.replaceHandle, 1234u);
    EXPECT_EQ(finished.nodeHandle, 99u);
}

TEST(UploadServiceTest, RootDestinationSentinelIsPassedThrough)
{
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(0, true))
        .WillRepeatedly(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, upload(::testing::_, 0, true, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(Result<UploadOutcome>::ok(UploadOutcome{1})));

    UploadService service(mockClient);

    // Act
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 0, true, 0);
}

TEST(UploadServiceTest, CanUploadToDelegatesToClientCheckUpload)
{
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(7, false))
        .WillOnce(::testing::Return(Result<void>::fail("nope", MegaErrorCode::kEAccess)));

    UploadService service(mockClient);

    // Act
    Result<void> result = service.canUploadTo(7, false);

    // Assert
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEAccess);
}

TEST(UploadServiceTest, FindNameCollisionsDelegatesToClientFindChildFiles)
{
    FileEntry existing;
    existing.name = "a.txt";
    existing.handle = 55;

    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, findChildFiles(7, false, std::vector<std::string>{"a.txt", "b.txt"}))
        .WillOnce(::testing::Return(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{existing})));

    UploadService service(mockClient);

    // Act
    Result<std::vector<FileEntry>> result =
        service.findNameCollisions(7, false, {"a.txt", "b.txt"});

    // Assert
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].handle, 55u);
}

TEST(UploadServiceTest, SynchronousUploadFailuresDrainTheQueueWithoutRecursing)
{
    // Companion to DestinationGoneDrainsEveryQueuedJobForThatDestination
    // above, which only covers the checkUpload() fast-fail. This one is the
    // other synchronous path: checkUpload passes, but IMegaClient::upload()
    // itself fails in-stack because the parent handle no longer resolves
    // (IMegaClient.h's delivery mode 3). onDone's auto-advance would nest one
    // frame per queued job without startNextIfIdle()'s trampoline.
    //
    // Job 1 is left in flight while 2..50 pile up behind it: an
    // all-synchronous queue drains one job per enqueue() call and never
    // stacks, so firing job 1's onDone is what triggers the cascade.
    auto mockClient = makeClient();
    std::function<void(Result<UploadOutcome>)> firstOnDone;
    int depth = 0;
    int maxDepth = 0;
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&firstOnDone))
        .WillRepeatedly(::testing::Invoke([&](const std::string&,
                                              std::uint64_t,
                                              bool,
                                              std::function<void(std::uint64_t, std::uint64_t)>,
                                              std::function<void(Result<UploadOutcome>)> onDone) {
            ++depth;
            maxDepth = std::max(maxDepth, depth);
            onDone(Result<UploadOutcome>::fail("gone", MegaErrorCode::kENoEnt));
            --depth;
        }));

    UploadService service(mockClient);
    int finishedCount = 0;
    service.setOnJobFinished([&](UploadJob) {
        ++finishedCount;
    });

    for (int i = 0; i < 50; ++i)
        service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 0);
    ASSERT_TRUE(static_cast<bool>(firstOnDone));

    // Act: job 1 finishes, and jobs 2..50 all fail the moment they start
    firstOnDone(Result<UploadOutcome>::fail("gone", MegaErrorCode::kENoEnt));

    // Assert
    EXPECT_EQ(finishedCount, 50);
    EXPECT_EQ(maxDepth, 1); // recursing would make this 49
    EXPECT_EQ(service.queueLength(), 0u);
}

TEST(UploadServiceTest, CheckUploadRejectionMidQueueDoesNotStopTheJobBehindIt)
{
    // The two drain tests above are homogeneous: every job takes the same exit.
    // This one interleaves them, because the checkUpload fast-fail leaves the
    // loop through `continue` rather than through onDone -- the path that has
    // to clear mAdvancing itself. Getting that wrong strands job 3 as Queued
    // with nobody left to start it, which no all-failing queue would show.
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(8, false))
        .WillRepeatedly(::testing::Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));
    std::function<void(Result<UploadOutcome>)> onDone1;
    std::function<void(Result<UploadOutcome>)> onDone3;
    EXPECT_CALL(*mockClient,
                upload(::testing::_, 7, false, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone1));
    EXPECT_CALL(*mockClient,
                upload(::testing::_, 9, false, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone3));

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    std::uint64_t id1 = service.enqueue("a", "a.txt", 7, false, 0);
    std::uint64_t id2 = service.enqueue("b", "b.txt", 8, false, 0);
    std::uint64_t id3 = service.enqueue("c", "c.txt", 9, false, 0);

    // Act: finishing job 1 hands the loop to job 2, which fast-fails, and the
    // same loop turn must carry on to job 3
    onDone1(Result<UploadOutcome>::ok(UploadOutcome{11}));

    // Assert: job 3 started (upload() was called for it) with job 2 failed
    ASSERT_TRUE(static_cast<bool>(onDone3));
    ASSERT_EQ(finished.size(), 2u);
    EXPECT_EQ(finished[0].id, id1);
    EXPECT_EQ(finished[0].state, UploadState::Completed);
    EXPECT_EQ(finished[1].id, id2);
    EXPECT_EQ(finished[1].state, UploadState::Failed);
    EXPECT_EQ(finished[1].errorCode, MegaErrorCode::kENoEnt);

    onDone3(Result<UploadOutcome>::ok(UploadOutcome{33}));
    ASSERT_EQ(finished.size(), 3u);
    EXPECT_EQ(finished[2].id, id3);
    EXPECT_EQ(finished[2].state, UploadState::Completed);
    EXPECT_EQ(finished[2].nodeHandle, 33u);
    EXPECT_TRUE(finished[2].errorMessage.empty());
    EXPECT_EQ(service.queueLength(), 0u);
}

TEST(UploadServiceTest, CompletionArrivingAfterTheQueueDrainedIsIgnored)
{
    // The `if (mQueue.empty()) return;` guard at the top of both callbacks --
    // MegaSdkClient::shutdown() completes everything still pending as it joins
    // the SDK thread, so an already-finished job can be completed a second
    // time and front() would be UB by then. Same shape as DownloadService's.
    auto mockClient = makeClient();
    std::function<void(std::uint64_t, std::uint64_t)> onProgress;
    std::function<void(Result<UploadOutcome>)> onDone;
    EXPECT_CALL(*mockClient,
                upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&onProgress),
                                   ::testing::SaveArg<4>(&onDone)));

    UploadService service(mockClient);
    int finishedCount = 0;
    int progressCount = 0;
    service.setOnJobFinished([&](UploadJob) {
        ++finishedCount;
    });
    service.setOnProgress([&](UploadJob) {
        ++progressCount;
    });

    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 0);
    onDone(Result<UploadOutcome>::ok(UploadOutcome{1}));
    ASSERT_EQ(finishedCount, 1);
    ASSERT_EQ(service.queueLength(), 0u);

    // Act: the same callbacks fire again against the now-empty queue
    onProgress(50, 100);
    onDone(Result<UploadOutcome>::fail("late", MegaErrorCode::kENoEnt));

    // Assert: both dropped silently
    EXPECT_EQ(finishedCount, 1);
    EXPECT_EQ(progressCount, 0);
    EXPECT_EQ(service.queueLength(), 0u);
    EXPECT_FALSE(service.currentJob().has_value());
}
