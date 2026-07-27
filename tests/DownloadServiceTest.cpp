#include "core/DownloadService.h"

#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(DownloadServiceTest, InitiallyHasNoCurrentJob)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    DownloadService service(mockClient);

    // Assert
    EXPECT_FALSE(service.hasCurrentJob());
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
    EXPECT_FALSE(service.hasCurrentJob());
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
    EXPECT_FALSE(service.hasCurrentJob());
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
    ASSERT_TRUE(service.hasCurrentJob());
    EXPECT_EQ(service.currentJob().totalBytes, 12345u);
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
