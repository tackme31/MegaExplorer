#include "core/DownloadService.h"

#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
    EXPECT_CALL(*mockClient, download(5, std::string("/tmp/a.txt"), ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(
            Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a (1).txt", false})));

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
    EXPECT_FALSE(finished.alreadyPresent);
    EXPECT_FALSE(service.currentJob().has_value());
}

TEST(DownloadServiceTest, EnqueueSuccessSurfacesAlreadyPresentWhenSdkSkippedIdenticalFile)
{
    // Arrange: an identical (fingerprint-matching) file already exists locally,
    // so the SDK completes without writing any bytes -- see
    // MegaApiImpl::CompleteFileDownloadBySkip.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(
            Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt", true})));

    DownloadService service(mockClient);
    DownloadJob finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished = std::move(job);
    });

    // Act
    service.enqueue(1, "a.txt", "/tmp/a.txt", 100);

    // Assert
    EXPECT_EQ(finished.state, DownloadState::Completed);
    EXPECT_TRUE(finished.alreadyPresent);
}

TEST(DownloadServiceTest, EnqueueFailurePropagatesErrorMessageCodeAndState)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(Result<DownloadOutcome>::fail("network error", 2)));

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
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::InvokeArgument<2>(std::uint64_t{50}, std::uint64_t{100}),
                             ::testing::InvokeArgument<3>(Result<DownloadOutcome>::ok(
                                 DownloadOutcome{"/tmp/a.txt", false}))));

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
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);

    // Act
    service.enqueue(1, "a.txt", "/tmp/a.txt", 12345);

    // Assert
    std::optional<DownloadJob> job = service.currentJob();
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->totalBytes, 12345u);
}

TEST(DownloadServiceTest, SecondEnqueueWhileFirstActiveDoesNotStartImmediatelyThenAutoStarts)
{
    // Arrange: capture both calls' callbacks instead of invoking them, so we
    // can control exactly when each transfer "finishes".
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<DownloadOutcome>)> onDone1;
    std::function<void(Result<DownloadOutcome>)> onDone2;

    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&onDone1)));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&onDone2)));

    DownloadService service(mockClient);

    // Act: enqueue both while nothing has finished yet
    std::uint64_t id1 = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 0);

    // Assert: only job 1 started; job 2 is queued and IMegaClient::download
    // was never called for it (onDone2 is still unset)
    ASSERT_FALSE(static_cast<bool>(onDone2));
    auto jobsBefore = service.jobs();
    ASSERT_EQ(jobsBefore.size(), 2u);
    EXPECT_EQ(jobsBefore[0].id, id1);
    EXPECT_EQ(jobsBefore[0].state, DownloadState::Active);
    EXPECT_EQ(jobsBefore[1].id, id2);
    EXPECT_EQ(jobsBefore[1].state, DownloadState::Queued);

    // Act: finish job 1
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt", false}));

    // Assert: job 2 auto-started (download() was called for it, captured via
    // SaveArg above -- onDone2 would still be unset otherwise)
    ASSERT_TRUE(static_cast<bool>(onDone2));
    auto jobsAfter = service.jobs();
    ASSERT_EQ(jobsAfter.size(), 1u);
    EXPECT_EQ(jobsAfter[0].id, id2);
    EXPECT_EQ(jobsAfter[0].state, DownloadState::Active);
}

TEST(DownloadServiceTest, HasJobForHandleFindsQueuedJob)
{
    // Arrange: never invoke the download() callback, so both jobs stay put
    // (one active, one queued) -- hasJobForHandle must find either.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);

    DownloadService service(mockClient);
    service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    service.enqueue(2, "b.txt", "/tmp/b.txt", 0);

    EXPECT_TRUE(service.hasJobForHandle(1));
    EXPECT_TRUE(service.hasJobForHandle(2));
}

TEST(DownloadServiceTest, HasJobForHandleReturnsFalseForUnknownHandle)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);
    service.enqueue(1, "a.txt", "/tmp/a.txt", 0);

    EXPECT_FALSE(service.hasJobForHandle(999));
}

TEST(DownloadServiceTest, JobsReturnsActiveJobFirstThenQueuedJobsInOrder)
{
    // Arrange: never invoke the download() callback, so all 3 jobs stay put.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);

    // Act
    std::uint64_t id1 = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 0);
    std::uint64_t id3 = service.enqueue(3, "c.txt", "/tmp/c.txt", 0);

    // Assert
    auto jobs = service.jobs();
    ASSERT_EQ(jobs.size(), 3u);
    EXPECT_EQ(jobs[0].id, id1);
    EXPECT_EQ(jobs[0].state, DownloadState::Active);
    EXPECT_EQ(jobs[1].id, id2);
    EXPECT_EQ(jobs[1].state, DownloadState::Queued);
    EXPECT_EQ(jobs[2].id, id3);
    EXPECT_EQ(jobs[2].state, DownloadState::Queued);
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
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<3>(&firstOnDone))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t,
                                              const std::string&,
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
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<3>(&onDone1));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<3>(&onDone2));
    EXPECT_CALL(*mockClient, download(3, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<3>(&onDone3));

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
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a (1).txt", false}));
    ASSERT_TRUE(static_cast<bool>(onDone2));
    onDone2(Result<DownloadOutcome>::fail("network error", 2));
    ASSERT_TRUE(static_cast<bool>(onDone3));
    onDone3(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/c.txt", true}));

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
    EXPECT_TRUE(finished[2].alreadyPresent);
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
    EXPECT_CALL(*mockClient, download(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::SaveArg<2>(&onProgress), ::testing::SaveArg<3>(&onDone)));

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
    onDone(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt", false}));
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
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SaveArg<2>(&onProgress1),
                                   ::testing::SaveArg<3>(&onDone1)));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);
    std::vector<DownloadJob> progressSnapshots;
    service.setOnProgress([&](DownloadJob job) {
        progressSnapshots.push_back(std::move(job));
    });

    service.enqueue(1, "a.txt", "/tmp/a.txt", 10);
    const std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 999);
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt", false}));

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
    EXPECT_CALL(*mockClient, download(1, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<3>(&onDone1));
    EXPECT_CALL(*mockClient, download(2, ::testing::_, ::testing::_, ::testing::_));

    DownloadService service(mockClient);
    std::vector<DownloadJob> finished;
    service.setOnJobFinished([&](DownloadJob job) {
        finished.push_back(std::move(job));
    });

    const std::uint64_t id1 = service.enqueue(1, "a.txt", "/tmp/a.txt", 0);
    const std::uint64_t id2 = service.enqueue(2, "b.txt", "/tmp/b.txt", 0);
    onDone1(Result<DownloadOutcome>::ok(DownloadOutcome{"/tmp/a.txt", false}));
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
