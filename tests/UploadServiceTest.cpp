#include "core/UploadService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>

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

constexpr std::size_t kSlots = UploadService::kMaxConcurrent;

using UploadDone = std::function<void(Result<UploadOutcome>)>;

// Captures each transfer's onDone instead of invoking it, so the test decides when
// a job finishes; expectedCalls also pins how many transfers may start at all.
void expectCapturedUploads(MockMegaClient& client,
                           std::vector<UploadDone>& onDone,
                           std::size_t expectedCalls)
{
    EXPECT_CALL(
        client,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(static_cast<int>(expectedCalls))
        .WillRepeatedly(
            ::testing::Invoke([&onDone](const std::string&,
                                        std::uint64_t,
                                        bool,
                                        std::uint64_t,
                                        std::function<void(std::uint64_t, std::uint64_t)>,
                                        UploadDone done) {
                onDone.push_back(std::move(done));
            }));
}

// Local paths and names run 1..count, all into the same destination folder.
std::vector<std::uint64_t> enqueueMany(UploadService& service, std::size_t count)
{
    std::vector<std::uint64_t> ids;
    for (std::size_t i = 1; i <= count; ++i)
    {
        const std::string name = "f" + std::to_string(i) + ".txt";
        ids.push_back(service.enqueue("/tmp/" + name, name, 7, false, 10));
    }
    return ids;
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
    EXPECT_CALL(
        *mockClient,
        upload(std::string("C:\\tmp\\a.txt"), 7, false, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{42})));

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
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<5>(Result<UploadOutcome>::fail("network error", 2)));

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
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_));

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
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::InvokeArgument<4>(std::uint64_t{50}, std::uint64_t{200}),
            ::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{1}))));

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

TEST(UploadServiceTest, EnqueuePastTheConcurrencyLimitWaitsThenAutoStarts)
{
    // Arrange: capture every call's completion callback instead of invoking it, so
    // we control exactly when each transfer "finishes".
    auto mockClient = makeClient();
    std::vector<UploadDone> onDone;
    expectCapturedUploads(*mockClient, onDone, kSlots + 1);

    UploadService service(mockClient);

    // Act: fill every slot, then one more
    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 1);

    // Assert: the last one is queued and IMegaClient::upload was never called for it
    ASSERT_EQ(onDone.size(), kSlots);
    const std::vector<UploadJob> before = service.jobs();
    ASSERT_EQ(before.size(), kSlots + 1);
    for (std::size_t i = 0; i < kSlots; ++i)
    {
        EXPECT_EQ(before[i].id, ids[i]);
        EXPECT_EQ(before[i].state, UploadState::Active);
    }
    EXPECT_EQ(before.back().id, ids.back());
    EXPECT_EQ(before.back().state, UploadState::Queued);

    // Act: free one slot
    onDone.front()(Result<UploadOutcome>::ok(UploadOutcome{1}));

    // Assert: the waiting job was promoted into it
    ASSERT_EQ(onDone.size(), kSlots + 1);
    const std::vector<UploadJob> after = service.jobs();
    ASSERT_EQ(after.size(), kSlots);
    EXPECT_EQ(after.back().id, ids.back());
    EXPECT_EQ(after.back().state, UploadState::Active);
}

TEST(UploadServiceTest, DestinationGoneAtStartTimeFailsWithoutCallingUpload)
{
    // Arrange: hover-time validation passed, but the folder is gone by the
    // time the job's turn comes.
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(7, false))
        .WillRepeatedly(::testing::Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
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
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
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

TEST(UploadServiceTest, TheCreatedNodeHandleReachesTheFinishedNotification)
{
    auto mockClient = makeClient();
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{99})));
    // An upload is one step: MEGA versions a same-named node itself, so nothing in
    // this service ever deletes anything.
    EXPECT_CALL(*mockClient, moveToRubbish(::testing::_, ::testing::_)).Times(0);

    UploadService service(mockClient);
    UploadJob finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished = std::move(job);
    });

    // Act
    service.enqueue("C:\\tmp\\a.txt", "a.txt", 7, false, 0);

    // Assert
    EXPECT_EQ(finished.nodeHandle, 99u);
}

TEST(UploadServiceTest, RootDestinationSentinelIsPassedThrough)
{
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(0, true))
        .WillRepeatedly(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient,
                upload(::testing::_, 0, true, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<5>(Result<UploadOutcome>::ok(UploadOutcome{1})));

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
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<5>(&firstOnDone))
        .WillRepeatedly(::testing::Invoke([&](const std::string&,
                                              std::uint64_t,
                                              bool,
                                              std::uint64_t,
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
    // with nobody left to start it, which no all-failing queue would show. The
    // slots are filled first so both jobs are genuinely waiting, which is what
    // puts them in one loop turn.
    auto mockClient = makeClient();
    EXPECT_CALL(*mockClient, checkUpload(8, false))
        .WillRepeatedly(::testing::Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));
    std::vector<UploadDone> filler;
    expectCapturedUploads(*mockClient, filler, kSlots);
    std::function<void(Result<UploadOutcome>)> onDone3;
    EXPECT_CALL(*mockClient,
                upload(::testing::_, 9, false, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<5>(&onDone3));

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    const std::vector<std::uint64_t> filled = enqueueMany(service, kSlots);
    std::uint64_t id2 = service.enqueue("b", "b.txt", 8, false, 0);
    std::uint64_t id3 = service.enqueue("c", "c.txt", 9, false, 0);

    // Act: finishing one running job hands the loop to job 2, which fast-fails,
    // and the same loop turn must carry on to job 3
    filler[0](Result<UploadOutcome>::ok(UploadOutcome{11}));

    // Assert: job 3 started (upload() was called for it) with job 2 failed
    ASSERT_TRUE(static_cast<bool>(onDone3));
    ASSERT_EQ(finished.size(), 2u);
    EXPECT_EQ(finished[0].id, filled[0]);
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

    for (std::size_t i = 1; i < kSlots; ++i)
        filler[i](Result<UploadOutcome>::ok(UploadOutcome{1}));
    EXPECT_EQ(service.queueLength(), 0u);
}

TEST(UploadServiceTest, CompletionArrivingAfterTheQueueDrainedIsIgnored)
{
    // The `!mActive` half of the guard at the top of both callbacks --
    // MegaSdkClient::shutdown() completes everything still pending as it joins
    // the SDK thread, so an already-finished job can be completed a second
    // time with nothing running by then. Same shape as DownloadService's.
    auto mockClient = makeClient();
    std::function<void(std::uint64_t, std::uint64_t)> onProgress;
    std::function<void(Result<UploadOutcome>)> onDone;
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::SaveArg<4>(&onProgress), ::testing::SaveArg<5>(&onDone)));

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

TEST(UploadServiceTest, StaleProgressFromAFinishedJobDoesNotTouchTheNextJob)
{
    // The job-id half of the guard, same shape as DownloadService's: job 1
    // finishes, job 2 is promoted, and only then does job 1's onProgress
    // arrive. Without the id check it lands on job 2.
    auto mockClient = makeClient();
    std::function<void(std::uint64_t, std::uint64_t)> onProgress1;
    std::function<void(Result<UploadOutcome>)> onDone1;
    EXPECT_CALL(
        *mockClient,
        upload(
            std::string("a"), ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::SaveArg<4>(&onProgress1), ::testing::SaveArg<5>(&onDone1)));
    EXPECT_CALL(*mockClient,
                upload(std::string("b"),
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_));

    UploadService service(mockClient);
    std::vector<UploadJob> progressSnapshots;
    service.setOnProgress([&](UploadJob job) {
        progressSnapshots.push_back(std::move(job));
    });

    service.enqueue("a", "a.txt", 7, false, 10);
    const std::uint64_t id2 = service.enqueue("b", "b.txt", 7, false, 999);
    onDone1(Result<UploadOutcome>::ok(UploadOutcome{1}));

    // Act
    onProgress1(50, 100);

    // Assert
    std::optional<UploadJob> active = service.currentJob();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->id, id2);
    EXPECT_EQ(active->transferredBytes, 0u);
    EXPECT_EQ(active->totalBytes, 999u);
    EXPECT_TRUE(progressSnapshots.empty());
}

TEST(UploadServiceTest, StaleCompletionFromAFinishedJobDoesNotFinishTheNextJob)
{
    // Completion side: a late onDone must not report job 2 as finished and
    // drop it while its transfer is still running.
    auto mockClient = makeClient();
    std::function<void(Result<UploadOutcome>)> onDone1;
    EXPECT_CALL(
        *mockClient,
        upload(
            std::string("a"), ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<5>(&onDone1));
    EXPECT_CALL(*mockClient,
                upload(std::string("b"),
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_));

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id1 = service.enqueue("a", "a.txt", 7, false, 0);
    const std::uint64_t id2 = service.enqueue("b", "b.txt", 7, false, 0);
    onDone1(Result<UploadOutcome>::ok(UploadOutcome{1}));
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, id1);

    // Act
    onDone1(Result<UploadOutcome>::fail("late", MegaErrorCode::kENoEnt));

    // Assert
    EXPECT_EQ(finished.size(), 1u);
    std::vector<UploadJob> remaining = service.jobs();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0].id, id2);
    EXPECT_EQ(remaining[0].state, UploadState::Active);
}

TEST(UploadServiceTest, StaleProgressIsIgnoredAcrossAJobDroppedByTheDestinationRecheck)
{
    // Covers the third path that writes mActive -- the checkUpload recheck in
    // startNextIfIdle() -- together with the id guard: job 1 finishes, job 2
    // fails its recheck and is dropped in-stack, job 3 is promoted, and only
    // then does job 1's onProgress arrive.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, checkUpload(7, false))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, checkUpload(8, false))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));

    std::function<void(std::uint64_t, std::uint64_t)> onProgress1;
    std::function<void(Result<UploadOutcome>)> onDone1;
    EXPECT_CALL(
        *mockClient,
        upload(
            std::string("a"), ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::SaveArg<4>(&onProgress1), ::testing::SaveArg<5>(&onDone1)));
    EXPECT_CALL(*mockClient,
                upload(std::string("c"),
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_));

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    std::vector<UploadJob> progressSnapshots;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });
    service.setOnProgress([&](UploadJob job) {
        progressSnapshots.push_back(std::move(job));
    });

    service.enqueue("a", "a.txt", 7, false, 0);
    service.enqueue("b", "b.txt", 8, false, 0); // destination is gone
    const std::uint64_t id3 = service.enqueue("c", "c.txt", 7, false, 999);

    ASSERT_EQ(finished.size(), 1u); // job 2 was rejected the moment it was promoted
    EXPECT_EQ(finished[0].state, UploadState::Failed);
    onDone1(Result<UploadOutcome>::ok(UploadOutcome{1}));
    ASSERT_EQ(finished.size(), 2u);

    // Act
    onProgress1(50, 100);

    // Assert: job 3 is running and untouched
    std::optional<UploadJob> active = service.currentJob();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->id, id3);
    EXPECT_EQ(active->totalBytes, 999u);
    EXPECT_TRUE(progressSnapshots.empty());
}

TEST(UploadServiceTest, CancelAllDropsPendingJobsAndAbortsEveryActiveTransfer)
{
    auto mockClient = makeClient();
    std::vector<UploadDone> onDone;
    // The queued job must never reach the SDK, so only the running ones count.
    expectCapturedUploads(*mockClient, onDone, kSlots);
    EXPECT_CALL(*mockClient, cancelUpload(::testing::_)).Times(static_cast<int>(kSlots));

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 1);

    // Act
    service.cancelAll();

    // Assert: same contract as DownloadService -- one notification per dropped job
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, ids.back());
    EXPECT_EQ(finished[0].state, UploadState::Cancelled);

    for (std::size_t i = 0; i < kSlots; ++i)
        onDone[i](Result<UploadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));
    ASSERT_EQ(finished.size(), kSlots + 1);
    EXPECT_EQ(finished.back().id, ids[kSlots - 1]);
    EXPECT_EQ(finished.back().state, UploadState::Cancelled);
    EXPECT_EQ(service.queueLength(), 0u);
}

TEST(UploadServiceTest, CancelDropsOneQueuedJobAndLeavesTheRestOfTheQueueAlone)
{
    auto mockClient = makeClient();
    std::vector<UploadDone> onDone;
    expectCapturedUploads(*mockClient, onDone, kSlots);
    EXPECT_CALL(*mockClient, cancelUpload(::testing::_)).Times(0);

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 2);

    // Act: the first of the two waiting jobs
    service.cancel(ids[kSlots]);

    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, ids[kSlots]);
    EXPECT_EQ(finished[0].state, UploadState::Cancelled);

    const std::vector<UploadJob> remaining = service.jobs();
    ASSERT_EQ(remaining.size(), kSlots + 1);
    EXPECT_EQ(remaining[0].id, ids[0]);
    EXPECT_EQ(remaining[0].state, UploadState::Active);
    EXPECT_EQ(remaining.back().id, ids.back());
    EXPECT_EQ(remaining.back().state, UploadState::Queued);
}

TEST(UploadServiceTest, AJobCancelledDuringItsDestinationRecheckReportsCancelledNotFailed)
{
    // The recheck is a blocking round-trip with the job already promoted, so a cancel
    // can land inside it -- naming a transfer that was never started, so the abort
    // reaches nothing. The rejection that follows must not be reported as a failure,
    // or UploadController counts the user's own stop as an error and raises a second
    // toast on top of the one cancelUploads() already gave.
    auto mockClient = makeClient();
    UploadService service(mockClient);
    EXPECT_CALL(*mockClient, checkUpload(8, false))
        .WillRepeatedly(::testing::InvokeWithoutArgs([&service] {
            // Ids are 1-based and this is the only enqueue, so the job being rechecked
            // is 1 -- enqueue() has not returned its id yet.
            service.cancel(1);
            return Result<void>::fail("gone", MegaErrorCode::kENoEnt);
        }));
    EXPECT_CALL(*mockClient, cancelUpload(::testing::_)).Times(::testing::AnyNumber());
    std::vector<UploadDone> never;
    expectCapturedUploads(*mockClient, never, 0);

    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id = service.enqueue("b", "b.txt", 8, false, 0);

    ASSERT_EQ(id, 1u);
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, id);
    EXPECT_EQ(finished[0].state, UploadState::Cancelled);
}

TEST(UploadServiceTest, TheTransferIdHandedToTheClientIsTheJobId)
{
    // Same contract as DownloadService's -- cancelUpload() is given the id the UI
    // holds, so it has to be the one upload() was started under.
    auto mockClient = makeClient();
    std::uint64_t startedUnder = 0;
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<3>(&startedUnder));

    UploadService service(mockClient);
    const std::uint64_t id = service.enqueue("/tmp/a.txt", "a.txt", 7, false, 0);

    EXPECT_EQ(startedUnder, id);
}

TEST(UploadServiceTest, CancellingTheActiveJobAbortsItAndTheQueueBehindItCarriesOn)
{
    auto mockClient = makeClient();
    std::function<void(Result<UploadOutcome>)> onDone1;
    EXPECT_CALL(*mockClient,
                upload(std::string("/tmp/a.txt"),
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_))
        .WillOnce(::testing::SaveArg<5>(&onDone1));
    EXPECT_CALL(*mockClient,
                upload(std::string("/tmp/b.txt"),
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_,
                       ::testing::_));
    EXPECT_CALL(*mockClient, cancelUpload(::testing::_));

    UploadService service(mockClient);
    std::vector<UploadJob> finished;
    service.setOnJobFinished([&](UploadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id1 = service.enqueue("/tmp/a.txt", "a.txt", 7, false, 10);
    const std::uint64_t id2 = service.enqueue("/tmp/b.txt", "b.txt", 7, false, 10);

    // Act
    service.cancel(id1);

    EXPECT_TRUE(finished.empty()); // the SDK still has to acknowledge the abort
    EXPECT_EQ(service.queueLength(), 2u);

    ASSERT_TRUE(onDone1);
    onDone1(Result<UploadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));

    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, id1);
    EXPECT_EQ(finished[0].state, UploadState::Cancelled);
    const std::optional<UploadJob> active = service.currentJob();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->id, id2);
    EXPECT_EQ(active->state, UploadState::Active);
}

TEST(UploadServiceTest, AThrowingClientCallLeavesTheQueueAbleToStartTheNextJob)
{
    // Same regression guard as DownloadServiceTest's of this name -- see there.
    auto mockClient = makeClient();
    std::vector<UploadDone> onDone;
    EXPECT_CALL(
        *mockClient,
        upload(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Throw(std::runtime_error("boom")))
        .WillRepeatedly(::testing::Invoke(
            [&onDone](const std::string&,
                      std::uint64_t,
                      bool,
                      std::uint64_t,
                      std::function<void(std::uint64_t, std::uint64_t)>,
                      UploadDone done) {
                onDone.push_back(std::move(done));
            }));

    UploadService service(mockClient);
    EXPECT_THROW(service.enqueue("/tmp/a.txt", "a.txt", 7, false, 10), std::runtime_error);

    // Act
    service.enqueue("/tmp/b.txt", "b.txt", 7, false, 10);

    // Assert
    EXPECT_EQ(onDone.size(), 1u);
}
