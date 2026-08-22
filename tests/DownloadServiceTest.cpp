#include "core/DownloadService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace
{

constexpr std::size_t kSlots = DownloadService::kMaxConcurrent;

using DownloadDone = std::function<void(Result<DownloadOutcome>)>;

// Captures each transfer's onDone instead of invoking it, so the test decides when
// a job finishes; expectedCalls also pins how many transfers may start at all.
void expectCapturedDownloads(MockMegaClient& client,
                             std::vector<DownloadDone>& onDone,
                             std::size_t expectedCalls)
{
    EXPECT_CALL(client,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(static_cast<int>(expectedCalls))
        .WillRepeatedly(
            ::testing::Invoke([&onDone](std::uint64_t,
                                        const std::string&,
                                        std::uint64_t,
                                        std::function<void(std::uint64_t, std::uint64_t)>,
                                        DownloadDone done) {
                onDone.push_back(std::move(done));
            }));
}

// Handles and names run 1..count, so a test can name a job by its ordinal.
std::vector<std::uint64_t> enqueueMany(DownloadService& service, std::size_t count)
{
    std::vector<std::uint64_t> ids;
    for (std::size_t i = 1; i <= count; ++i)
    {
        const std::string name = "f" + std::to_string(i) + ".txt";
        ids.push_back(service.enqueue(i, name, "/tmp/" + name, 10));
    }
    return ids;
}

} // namespace

TEST(DownloadServiceTest, SafeLocalFileNameKeepsOnlyTheLeafOfATraversalName)
{
    EXPECT_EQ(DownloadService::safeLocalFileName("..\\..\\evil.exe"), "evil.exe");
    EXPECT_EQ(DownloadService::safeLocalFileName("../../evil.exe"), "evil.exe");
    EXPECT_EQ(DownloadService::safeLocalFileName("C:\\Windows\\System32\\x.dll"), "x.dll");
    // Drive-relative: no separator to cut at, so the "C:" needs its own step.
    EXPECT_EQ(DownloadService::safeLocalFileName("C:evil.exe"), "evil.exe");
}

TEST(DownloadServiceTest, SafeLocalFileNameReplacesForbiddenAndControlCharacters)
{
    EXPECT_EQ(DownloadService::safeLocalFileName("a<b>c:d?.txt"), "a_b_c_d_.txt");
    EXPECT_EQ(DownloadService::safeLocalFileName(std::string("a\x01") + "b.txt"), "a_b.txt");
}

TEST(DownloadServiceTest, SafeLocalFileNameStripsTrailingDotsAndSpaces)
{
    EXPECT_EQ(DownloadService::safeLocalFileName("evil.exe. "), "evil.exe");
}

TEST(DownloadServiceTest, SafeLocalFileNameFallsBackWhenNothingUsableIsLeft)
{
    EXPECT_EQ(DownloadService::safeLocalFileName(""), "download");
    EXPECT_EQ(DownloadService::safeLocalFileName("."), "download");
    EXPECT_EQ(DownloadService::safeLocalFileName(".."), "download");
    EXPECT_EQ(DownloadService::safeLocalFileName("..."), "download");
    EXPECT_EQ(DownloadService::safeLocalFileName("../"), "download");
}

TEST(DownloadServiceTest, SafeLocalFileNameSidestepsWindowsReservedDeviceNames)
{
    EXPECT_EQ(DownloadService::safeLocalFileName("CON"), "_CON");
    EXPECT_EQ(DownloadService::safeLocalFileName("con.txt"), "_con.txt");
    EXPECT_EQ(DownloadService::safeLocalFileName("COM1.log"), "_COM1.log");
    // Only exact stems are reserved.
    EXPECT_EQ(DownloadService::safeLocalFileName("CONS.txt"), "CONS.txt");
}

TEST(DownloadServiceTest, SafeLocalFileNameLeavesOrdinaryNamesAlone)
{
    EXPECT_EQ(DownloadService::safeLocalFileName("report.pdf"), "report.pdf");
    EXPECT_EQ(DownloadService::safeLocalFileName(".bashrc"), ".bashrc");
    // Multi-byte UTF-8 must survive byte-wise scanning untouched. Spelled as
    // raw bytes so no source/execution charset setting can change what is
    // actually being tested.
    const std::string japanese = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt";
    EXPECT_EQ(DownloadService::safeLocalFileName(japanese), japanese);
}

TEST(DownloadServiceTest, InitiallyHasNoCurrentJob)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    DownloadService service(mockClient);

    // Assert
    EXPECT_FALSE(service.currentJob().has_value());
    EXPECT_TRUE(service.jobs().empty());
}

TEST(DownloadServiceTest, EnqueueSuccessNotifiesJobFinishedWithCompletedStateAndResolvedPath)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient,
                download(5, std::string("/tmp/a.txt"), ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(
            Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a (1).txt"})));

    DownloadService service(mockClient);
    bool finishedCalled = false;
    DownloadJob finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finishedCalled = true;
        finished = std::move(job);
    });

    // Act
    std::uint64_t id = service.enqueue(5, "a.txt", "/tmp/a.txt", 100);

    // Assert: collision-rename is surfaced via resolvedLocalPath, even though
    // the requested destinationPath was "/tmp/a.txt"
    ASSERT_TRUE(finishedCalled);
    EXPECT_EQ(finished.id, id);
    EXPECT_EQ(finished.state, DownloadState::Completed);
    EXPECT_EQ(finished.resolvedLocalPath, "/tmp/a (1).txt");
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(DownloadServiceTest, EnqueueFailurePropagatesErrorMessageCodeAndState)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(Result<DownloadOutcome>::fail("network error", 2)));

    DownloadService service(mockClient);
    bool finishedCalled = false;
    DownloadJob finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finishedCalled = true;
        finished = std::move(job);
    });

    // Act
    service.enqueue(1, "a.txt", "/tmp/a.txt", 100);

    // Assert
    ASSERT_TRUE(finishedCalled);
    EXPECT_EQ(finished.state, DownloadState::Failed);
    EXPECT_EQ(finished.errorMessage, "network error");
    EXPECT_EQ(finished.errorCode, 2);
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(DownloadServiceTest, ProgressCallbackForwardsBytesBeforeCompletion)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::InvokeArgument<3>(std::uint64_t{50}, std::uint64_t{100}),
                             ::testing::InvokeArgument<4>(
                                 Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt"}))));

    DownloadService service(mockClient);
    std::vector<DownloadJob> progressSnapshots;
    service.setOnProgress([&](DownloadJob job) {
        progressSnapshots.push_back(std::move(job));
    });

    // Act
    service.enqueue(1, "a.txt", "/tmp/a.txt", 0);

    // Assert: progress observed exactly once, before completion
    ASSERT_EQ(progressSnapshots.size(), 1u);
    EXPECT_EQ(progressSnapshots[0].transferredBytes, 50u);
    EXPECT_EQ(progressSnapshots[0].totalBytes, 100u);
}

TEST(DownloadServiceTest, EnqueueSeedsTotalBytesFromExpectedTotalBytesBeforeFirstProgressTick)
{
    // Arrange: mocked download() doesn't invoke either callback, so this
    // observes state exactly as enqueue() left it.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);

    // Act
    service.enqueue(1, "a.txt", "/tmp/a.txt", 12345);

    // Assert
    std::optional<DownloadJob> job = service.currentJob();
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->totalBytes, 12345u);
}

TEST(DownloadServiceTest, EnqueuePastTheConcurrencyLimitWaitsThenAutoStarts)
{
    // Arrange: capture every call's onDone instead of invoking it, so we control
    // exactly when each transfer "finishes".
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    expectCapturedDownloads(*mockClient, onDone, kSlots + 1);

    DownloadService service(mockClient);

    // Act: fill every slot, then one more
    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 1);

    // Assert: the last one is queued and IMegaClient::download was never called
    // for it -- only kSlots onDones have been captured
    ASSERT_EQ(onDone.size(), kSlots);
    const std::vector<DownloadJob> before = service.jobs();
    ASSERT_EQ(before.size(), kSlots + 1);
    for (std::size_t i = 0; i < kSlots; ++i)
    {
        EXPECT_EQ(before[i].id, ids[i]);
        EXPECT_EQ(before[i].state, DownloadState::Active);
    }
    EXPECT_EQ(before.back().id, ids.back());
    EXPECT_EQ(before.back().state, DownloadState::Queued);

    // Act: free one slot
    onDone.front()(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt"}));

    // Assert: the waiting job was promoted into it
    ASSERT_EQ(onDone.size(), kSlots + 1);
    const std::vector<DownloadJob> after = service.jobs();
    ASSERT_EQ(after.size(), kSlots);
    EXPECT_EQ(after.back().id, ids.back());
    EXPECT_EQ(after.back().state, DownloadState::Active);
}

TEST(DownloadServiceTest, HasJobForHandleFindsQueuedJob)
{
    // Arrange: never invoke the download() callback, so the jobs stay put -- the
    // slots full and one waiting behind them. hasJobForHandle must find either.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    expectCapturedDownloads(*mockClient, onDone, kSlots);

    DownloadService service(mockClient);
    enqueueMany(service, kSlots + 1);

    EXPECT_TRUE(service.hasJobForHandle(1));                                      // active
    EXPECT_TRUE(service.hasJobForHandle(static_cast<std::uint64_t>(kSlots)));     // active
    EXPECT_TRUE(service.hasJobForHandle(static_cast<std::uint64_t>(kSlots) + 1)); // queued
}

TEST(DownloadServiceTest, HasJobForHandleReturnsFalseForUnknownHandle)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);
    service.enqueue(1, "a.txt", "/tmp/a.txt", 0);

    EXPECT_FALSE(service.hasJobForHandle(999));
}

TEST(DownloadServiceTest, JobsReturnsActiveJobsFirstThenQueuedJobsInOrder)
{
    // Arrange: never invoke the download() callback, so every job stays put.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    expectCapturedDownloads(*mockClient, onDone, kSlots);

    DownloadService service(mockClient);

    // Act
    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 2);

    // Assert
    const std::vector<DownloadJob> jobs = service.jobs();
    ASSERT_EQ(jobs.size(), kSlots + 2);
    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        EXPECT_EQ(jobs[i].id, ids[i]);
        EXPECT_EQ(jobs[i].state, i < kSlots ? DownloadState::Active : DownloadState::Queued);
    }
}

TEST(DownloadServiceTest, SynchronousFailuresDrainTheQueueWithoutRecursing)
{
    // Regression guard for startNextIfIdle()'s re-entrancy trampoline.
    // IMegaClient::download() fails *in-stack* when the handle no longer
    // resolves (IMegaClient.h's delivery mode 3 -- logged out mid-download, or
    // the folder deleted from another client), so onDone's auto-advance would
    // otherwise nest one frame per queued job.
    //
    // The first job must complete *asynchronously* for the nesting to build:
    // an all-synchronous queue drains one job per enqueue() call and never
    // stacks. So job 1 is left in flight while 2..50 pile up behind it, and
    // firing its onDone is what triggers the cascade.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<DownloadOutcome>)> firstOnDone;
    int depth = 0;
    int maxDepth = 0;
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&firstOnDone))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t,
                                              const std::string&,
                                              std::uint64_t,
                                              std::function<void(std::uint64_t, std::uint64_t)>,
                                              std::function<void(Result<DownloadOutcome>)> onDone) {
            ++depth;
            maxDepth = std::max(maxDepth, depth);
            onDone(Result<DownloadOutcome>::fail("gone", 2));
            --depth;
        }));

    DownloadService service(mockClient);
    int finishedCount = 0;
    service.setOnJobFinished([&](DownloadJob) {
        ++finishedCount;
    });

    for (int i = 0; i < 50; ++i)
        service.enqueue(7, "a.txt", "/tmp/a.txt", 0);
    ASSERT_TRUE(static_cast<bool>(firstOnDone));

    // Act: job 1 finishes, and jobs 2..50 all fail the moment they start
    firstOnDone(Result<DownloadOutcome>::fail("gone", 2));

    // Assert
    EXPECT_EQ(finishedCount, 50);
    EXPECT_EQ(maxDepth, 1); // recursing would make this 49
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(DownloadServiceTest, MixedSuccessAndFailureQueueKeepsPerJobResultFieldsSeparate)
{
    // Every other queue test drains an all-success or an all-failure queue, so
    // nothing so far would notice the completion callback writing a failure
    // into the wrong job: it edits mQueue.front() in place and only then
    // erases it, and the job behind inherits whatever the struct still holds.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<DownloadOutcome>)> onDone1;
    std::function<void(Result<DownloadOutcome>)> onDone2;
    std::function<void(Result<DownloadOutcome>)> onDone3;
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone1));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone2));
    EXPECT_CALL(*mockClient, download(3, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone3));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    std::uint64_t id1 = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 0);
    std::uint64_t id3 = service.enqueue(3, "c.txt", "/tmp/c.txt", 0);

    // Act: success, then failure, then success -- each one auto-advancing to
    // the next (an unstarted job's onDone would still be unset).
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a (1).txt"}));
    ASSERT_TRUE(static_cast<bool>(onDone2));
    onDone2(Result<DownloadOutcome>::fail("network error", 2));
    ASSERT_TRUE(static_cast<bool>(onDone3));
    onDone3(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/c.txt"}));

    // Assert: notified in enqueue order, and the failure in the middle left no
    // trace on the job behind it
    ASSERT_EQ(finished.size(), 3u);
    EXPECT_EQ(finished[0].id, id1);
    EXPECT_EQ(finished[0].state, DownloadState::Completed);
    EXPECT_EQ(finished[0].resolvedLocalPath, "/tmp/a (1).txt");
    EXPECT_TRUE(finished[0].errorMessage.empty());

    EXPECT_EQ(finished[1].id, id2);
    EXPECT_EQ(finished[1].state, DownloadState::Failed);
    EXPECT_EQ(finished[1].errorMessage, "network error");
    EXPECT_EQ(finished[1].errorCode, 2);
    EXPECT_TRUE(finished[1].resolvedLocalPath.empty());

    EXPECT_EQ(finished[2].id, id3);
    EXPECT_EQ(finished[2].state, DownloadState::Completed);
    EXPECT_EQ(finished[2].resolvedLocalPath, "/tmp/c.txt");
    EXPECT_TRUE(finished[2].errorMessage.empty());
    EXPECT_EQ(finished[2].errorCode, 0);

    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(DownloadServiceTest, CompletionArrivingAfterTheQueueDrainedIsIgnored)
{
    // The `!mActive` half of the guard at the top of both callbacks.
    // MegaSdkClient::shutdown() completes everything still pending as it joins
    // the SDK thread, so a job that already finished can be completed a second
    // time, with nothing running by then.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(std::uint64_t, std::uint64_t)> onProgress;
    std::function<void(Result<DownloadOutcome>)> onDone;
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::SaveArg<3>(&onProgress), ::testing::SaveArg<4>(&onDone)));

    DownloadService service(mockClient);
    int finishedCount = 0;
    int progressCount = 0;
    service.setOnJobFinished([&](DownloadJob) {
        ++finishedCount;
    });
    service.setOnProgress([&](DownloadJob) {
        ++progressCount;
    });

    service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    onDone(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt"}));
    ASSERT_EQ(finishedCount, 1);
    ASSERT_TRUE(service.jobs().empty());

    // Act: the same callbacks fire again against the now-empty queue
    onProgress(50, 100);
    onDone(Result<DownloadOutcome>::fail("late", 2));

    // Assert: both dropped silently -- no second notification, no new job
    EXPECT_EQ(finishedCount, 1);
    EXPECT_EQ(progressCount, 0);
    EXPECT_TRUE(service.jobs().empty());
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(DownloadServiceTest, StaleProgressFromAFinishedJobDoesNotTouchTheNextJob)
{
    // The job-id half of the guard. Job 1 finishes, job 2 is promoted in its
    // place, and only then does job 1's onProgress arrive -- exactly what a
    // cancel(jobId) would produce routinely. Without the id check, job 1's
    // byte counts land on job 2.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(std::uint64_t, std::uint64_t)> onProgress1;
    std::function<void(Result<DownloadOutcome>)> onDone1;
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::SaveArg<3>(&onProgress1), ::testing::SaveArg<4>(&onDone1)));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);
    std::vector<DownloadJob> progressSnapshots;
    service.setOnProgress([&](DownloadJob job) {
        progressSnapshots.push_back(std::move(job));
    });

    service.enqueue(1, "a.txt", "/tmp/a.txt", 10);
    const std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 999);
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt"}));

    // Act: job 1's transfer reports progress after it has already finished
    onProgress1(50, 100);

    // Assert: job 2 still shows its seeded numbers, and nobody was notified
    std::optional<DownloadJob> active = service.currentJob();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->id, id2);
    EXPECT_EQ(active->transferredBytes, 0u);
    EXPECT_EQ(active->totalBytes, 999u);
    EXPECT_TRUE(progressSnapshots.empty());
}

TEST(DownloadServiceTest, StaleCompletionFromAFinishedJobDoesNotFinishTheNextJob)
{
    // Same setup, completion side: without the id check the late onDone would
    // report job 2 as finished and drop it, losing a download that is still
    // running.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<DownloadOutcome>)> onDone1;
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone1));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id1 = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    const std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 0);
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt"}));
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, id1);

    // Act: job 1's transfer completes a second time
    onDone1(Result<DownloadOutcome>::fail("late", 2));

    // Assert: dropped -- job 2 is untouched and still running
    EXPECT_EQ(finished.size(), 1u);
    std::vector<DownloadJob> remaining = service.jobs();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0].id, id2);
    EXPECT_EQ(remaining[0].state, DownloadState::Active);
}

TEST(DownloadServiceTest, CancelAllDropsPendingJobsAndAbortsEveryActiveTransfer)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    // The queued job must never reach the SDK, so only the running ones count.
    expectCapturedDownloads(*mockClient, onDone, kSlots);
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_)).Times(static_cast<int>(kSlots));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 1);

    // Act
    service.cancelAll();

    // Assert: the waiting job reports once, without ever having started
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, ids.back());
    EXPECT_EQ(finished[0].state, DownloadState::Cancelled);

    // The running ones end only when the SDK acknowledges each abort, and their
    // kEIncomplete is read as cancelled rather than failed.
    for (std::size_t i = 0; i < kSlots; ++i)
        onDone[i](Result<DownloadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));

    ASSERT_EQ(finished.size(), kSlots + 1);
    EXPECT_EQ(finished.back().id, ids[kSlots - 1]);
    EXPECT_EQ(finished.back().state, DownloadState::Cancelled);
    EXPECT_FALSE(service.currentJob().has_value());
    EXPECT_TRUE(service.jobs().empty());
}

TEST(DownloadServiceTest, CancelAllWithNothingInFlightNeitherCallsTheClientNorNotifies)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_)).Times(0);

    DownloadService service(mockClient);
    bool notified = false;
    service.setOnJobFinished([&](DownloadJob) {
        notified = true;
    });

    service.cancelAll();

    EXPECT_FALSE(notified);
}

TEST(DownloadServiceTest, CancelArrivingWhileAJobIsStartingIsReAssertedOnceItsTokenExists)
{
    // The SDK only learns about a transfer inside IMegaClient::download(), so a
    // cancel that lands during that call reaches the *previous* transfer's token.
    // Without the re-assert the click is swallowed and the transfer runs to
    // completion.
    auto mockClient = std::make_shared<MockMegaClient>();
    DownloadService service(mockClient);

    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeWithoutArgs([&] {
            service.cancelAll();
        }));
    // Once from cancelAll() itself, once from the re-assert after download() returns.
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_)).Times(2);

    service.enqueue(1, "a.txt", "/tmp/a.txt", 10);
}

TEST(DownloadServiceTest, CancelDropsOneQueuedJobAndLeavesTheRestOfTheQueueAlone)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    // Nothing is in flight for this cancel to abort, and no further download() may
    // start: the jobs left keep their places.
    expectCapturedDownloads(*mockClient, onDone, kSlots);
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_)).Times(0);

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    const std::vector<std::uint64_t> ids = enqueueMany(service, kSlots + 2);

    // Act: the first of the two waiting jobs
    service.cancel(ids[kSlots]);

    // Assert: one notification for the dropped job, exactly as cancelAll() gives
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, ids[kSlots]);
    EXPECT_EQ(finished[0].state, DownloadState::Cancelled);

    const std::vector<DownloadJob> remaining = service.jobs();
    ASSERT_EQ(remaining.size(), kSlots + 1);
    EXPECT_EQ(remaining[0].id, ids[0]);
    EXPECT_EQ(remaining[0].state, DownloadState::Active);
    EXPECT_EQ(remaining.back().id, ids.back());
    EXPECT_EQ(remaining.back().state, DownloadState::Queued);
}

TEST(DownloadServiceTest, CancellingTheActiveJobAbortsItAndTheQueueBehindItCarriesOn)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<DownloadOutcome>)> onDone1;
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<4>(&onDone1));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_, ::testing::_));
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id1 = service.enqueue(1, "a.txt", "/tmp/a.txt", 10);
    const std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 10);

    // Act
    service.cancel(id1);

    // Nothing has finished yet -- only the SDK can say when the transfer stopped.
    EXPECT_TRUE(finished.empty());
    EXPECT_EQ(service.jobs().size(), 2u);

    ASSERT_TRUE(onDone1);
    onDone1(Result<DownloadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));

    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, id1);
    EXPECT_EQ(finished[0].state, DownloadState::Cancelled);
    // The job behind it starts, which is the whole difference from cancelAll().
    const std::optional<DownloadJob> active = service.currentJob();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->id, id2);
    EXPECT_EQ(active->state, DownloadState::Active);
}

TEST(DownloadServiceTest, AJobThatFailsBeforeItsAbortLandsStillReportsCancelled)
{
    // The abort names a transfer the client has not created yet, so it reaches
    // nothing -- and if the transfer then fails for its own reason, kEIncomplete
    // never comes. Without the cancel-wins rule the user's own stop is reported as
    // an error.
    auto mockClient = std::make_shared<MockMegaClient>();
    DownloadService service(mockClient);
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([&service](std::uint64_t,
                                               const std::string&,
                                               std::uint64_t transferId,
                                               std::function<void(std::uint64_t, std::uint64_t)>,
                                               DownloadDone done) {
            service.cancel(transferId);
            done(Result<DownloadOutcome>::fail("gone", MegaErrorCode::kENoEnt));
        }));
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_)).Times(::testing::AnyNumber());

    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);

    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, id);
    EXPECT_EQ(finished[0].state, DownloadState::Cancelled);
}

TEST(DownloadServiceTest, TheTransferIdHandedToTheClientIsTheJobId)
{
    // The whole per-transfer cancel rests on this: cancelDownload() is given the id
    // the UI holds, so it has to be the same one download() was started under.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::uint64_t startedUnder = 0;
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&startedUnder));

    DownloadService service(mockClient);
    const std::uint64_t id = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);

    EXPECT_EQ(startedUnder, id);
}

TEST(DownloadServiceTest, CancellingOneRunningJobAbortsOnlyThatTransfer)
{
    // What the whole-direction cancelDownload() could not do once transfers run in
    // parallel: stop one of them and leave its neighbours alone.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    expectCapturedDownloads(*mockClient, onDone, 2);
    std::vector<std::uint64_t> aborted;
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_))
        .WillRepeatedly(::testing::Invoke([&aborted](std::uint64_t id) {
            aborted.push_back(id);
        }));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });
    const std::vector<std::uint64_t> ids = enqueueMany(service, 2);

    // Act
    service.cancel(ids[1]);

    ASSERT_EQ(aborted.size(), 1u);
    EXPECT_EQ(aborted[0], ids[1]);

    onDone[1](Result<DownloadOutcome>::fail("Transfer cancelled", MegaErrorCode::kEIncomplete));
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished[0].id, ids[1]);
    EXPECT_EQ(finished[0].state, DownloadState::Cancelled);

    // The neighbour is still running
    const std::vector<DownloadJob> remaining = service.jobs();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0].id, ids[0]);
    EXPECT_EQ(remaining[0].state, DownloadState::Active);
}

TEST(DownloadServiceTest, CancelOfAnIdThatAlreadyFinishedNeitherNotifiesNorReachesTheClient)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<4>(
            Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt"})));
    EXPECT_CALL(*mockClient, cancelDownload(::testing::_)).Times(0);

    DownloadService service(mockClient);
    int finishedCount = 0;
    service.setOnJobFinished([&](DownloadJob) {
        ++finishedCount;
    });

    const std::uint64_t id = service.enqueue(1, "a.txt", "/tmp/a.txt", 10);
    ASSERT_EQ(finishedCount, 1);

    // Act: a row left over in the UI after its job settled
    service.cancel(id);

    EXPECT_EQ(finishedCount, 1); // still exactly one completion for that job
}

TEST(DownloadServiceTest, AThrowingClientCallLeavesTheQueueAbleToStartTheNextJob)
{
    // Regression guard for the re-entrancy flag's lifetime: it used to be cleared by
    // hand at the one exit that returns normally, so an exception from anything
    // startNextIfIdle() calls left it set forever and every later enqueue() bounced
    // off the guard -- ids kept being handed out with no transfer ever starting.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Throw(std::runtime_error("boom")))
        .WillRepeatedly(
            ::testing::Invoke([&onDone](std::uint64_t,
                                        const std::string&,
                                        std::uint64_t,
                                        std::function<void(std::uint64_t, std::uint64_t)>,
                                        DownloadDone done) {
                onDone.push_back(std::move(done));
            }));

    DownloadService service(mockClient);
    EXPECT_THROW(service.enqueue(1, "a.txt", "/tmp/a.txt", 10), std::runtime_error);

    // Act
    service.enqueue(2, "b.txt", "/tmp/b.txt", 10);

    // Assert
    EXPECT_EQ(onDone.size(), 1u);
}

TEST(DownloadServiceTest, AThrowingStartGivesItsSlotBackAndReportsTheJob)
{
    // The flag guard alone did not cover this: the job is moved into mActive before
    // the client call, so a throw used to leave it there for good -- kMaxConcurrent
    // of those and no download starts again -- with nothing ever reporting it.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<DownloadDone> onDone;
    EXPECT_CALL(*mockClient,
                download(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Throw(std::runtime_error("boom")))
        .WillRepeatedly(
            ::testing::Invoke([&onDone](std::uint64_t,
                                        const std::string&,
                                        std::uint64_t,
                                        std::function<void(std::uint64_t, std::uint64_t)>,
                                        DownloadDone done) {
                onDone.push_back(std::move(done));
            }));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&finished](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    // Act
    EXPECT_THROW(service.enqueue(1, "a.txt", "/tmp/a.txt", 10), std::runtime_error);

    // Assert
    ASSERT_EQ(finished.size(), 1u);
    EXPECT_EQ(finished.front().state, DownloadState::Failed);
    EXPECT_FALSE(finished.front().errorMessage.empty());
    EXPECT_TRUE(service.jobs().empty());
    enqueueMany(service, kSlots); // every slot is still free, so all of these start
    EXPECT_EQ(onDone.size(), kSlots);
}
