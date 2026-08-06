#include "core/ThumbnailService.h"

#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(ThumbnailServiceTest, InitialRequestCallsSdkAndReturnsResult)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getThumbnail(7, std::string("/tmp/7.jpg"), ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok("/tmp/7.jpg")));

    ThumbnailService service(mockClient);
    bool called = false;
    Result<std::string> received;

    // Act
    service.request(7, "/tmp/7.jpg", [&](Result<std::string> result) {
        called = true;
        received = std::move(result);
    });

    // Assert
    ASSERT_TRUE(called);
    EXPECT_TRUE(received.success);
    EXPECT_EQ(received.value(), "/tmp/7.jpg");
}

TEST(ThumbnailServiceTest, SecondRequestForCachedHandleDoesNotCallSdkAgain)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getThumbnail(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok("/tmp/7.jpg")));

    ThumbnailService service(mockClient);
    service.request(7, "/tmp/7.jpg", [](Result<std::string>) {});

    // Act: second request for the same handle, after the first completed
    bool called = false;
    Result<std::string> received;
    service.request(7, "/tmp/7.jpg", [&](Result<std::string> result) {
        called = true;
        received = std::move(result);
    });

    // Assert: served from cache, no second SDK call (enforced by Times(1) above)
    ASSERT_TRUE(called);
    EXPECT_TRUE(received.success);
    EXPECT_EQ(received.value(), "/tmp/7.jpg");
}

TEST(ThumbnailServiceTest, DuplicateInFlightRequestAttachesCallbackWithoutDuplicateSdkCall)
{
    // Arrange: capture the SDK callback instead of invoking it, so the first
    // request is still in flight when the second one arrives.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> onDone;
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::SaveArg<2>(&onDone));

    ThumbnailService service(mockClient);
    bool firstCalled = false;
    bool secondCalled = false;
    Result<std::string> firstResult;
    Result<std::string> secondResult;

    // Act: two requests for the same handle before the SDK call finishes
    service.request(7, "/tmp/7.jpg", [&](Result<std::string> result) {
        firstCalled = true;
        firstResult = std::move(result);
    });
    service.request(7, "/tmp/7.jpg", [&](Result<std::string> result) {
        secondCalled = true;
        secondResult = std::move(result);
    });

    // Assert: neither callback fired yet, SDK was called exactly once
    ASSERT_FALSE(firstCalled);
    ASSERT_FALSE(secondCalled);
    ASSERT_TRUE(static_cast<bool>(onDone));

    // Act: the single in-flight SDK call finishes
    onDone(Result<std::string>::ok("/tmp/7.jpg"));

    // Assert: both callers received the result
    EXPECT_TRUE(firstCalled);
    EXPECT_TRUE(secondCalled);
    EXPECT_TRUE(firstResult.success);
    EXPECT_TRUE(secondResult.success);
    EXPECT_EQ(firstResult.value(), "/tmp/7.jpg");
    EXPECT_EQ(secondResult.value(), "/tmp/7.jpg");
}

TEST(ThumbnailServiceTest, RequestsBeyondMaxConcurrentAreQueuedThenAutoStart)
{
    // Arrange: maxConcurrent = 2, so a 3rd distinct handle must wait.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> onDone1;
    std::function<void(Result<std::string>)> onDone3;

    EXPECT_CALL(*mockClient, getThumbnail(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&onDone1));
    EXPECT_CALL(*mockClient, getThumbnail(2, ::testing::_, ::testing::_));
    EXPECT_CALL(*mockClient, getThumbnail(3, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&onDone3));

    ThumbnailService service(mockClient, /*maxConcurrent=*/2);

    // Act: request 3 distinct handles back-to-back
    service.request(1, "/tmp/1.jpg", [](Result<std::string>) {});
    service.request(2, "/tmp/2.jpg", [](Result<std::string>) {});
    service.request(3, "/tmp/3.jpg", [](Result<std::string>) {});

    // Assert: handle 3 hasn't reached the SDK yet -- it's queued behind the
    // maxConcurrent=2 cap
    ASSERT_FALSE(static_cast<bool>(onDone3));

    // Act: handle 1 finishes, freeing a slot
    ASSERT_TRUE(static_cast<bool>(onDone1));
    onDone1(Result<std::string>::ok("/tmp/1.jpg"));

    // Assert: handle 3 auto-started
    ASSERT_TRUE(static_cast<bool>(onDone3));
}

TEST(ThumbnailServiceTest, FailedRequestIsNotCachedAndRetriedOnNextRequest)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::fail("no thumbnail", 2)))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok("/tmp/7.jpg")));

    ThumbnailService service(mockClient);
    Result<std::string> firstResult;
    Result<std::string> secondResult;

    // Act
    service.request(7, "/tmp/7.jpg", [&](Result<std::string> result) {
        firstResult = std::move(result);
    });
    service.request(7, "/tmp/7.jpg", [&](Result<std::string> result) {
        secondResult = std::move(result);
    });

    // Assert: first failed and wasn't cached, so the second request hit the
    // SDK again (enforced by the two WillOnce()s above) and succeeded.
    EXPECT_FALSE(firstResult.success);
    EXPECT_EQ(firstResult.errorMessage, "no thumbnail");
    EXPECT_TRUE(secondResult.success);
    EXPECT_EQ(secondResult.value(), "/tmp/7.jpg");
}

TEST(ThumbnailServiceTest, SynchronousFailuresDrainTheQueueWithoutRecursing)
{
    // Regression guard for startNextIfCapacity()'s re-entrancy trampoline.
    // IMegaClient::getThumbnail() fails in-stack when the handle no longer
    // resolves (IMegaClient.h's delivery mode 3), which a fast grid scroll can
    // queue up by the dozen; finishJob()'s auto-advance would otherwise nest
    // one frame per queued handle.
    //
    // Handle 1 is left in flight while 2..50 pile up behind it: an
    // all-synchronous queue drains one job per request() call and never
    // stacks, so firing handle 1's onDone is what triggers the cascade.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> firstOnDone;
    int depth = 0;
    int maxDepth = 0;
    EXPECT_CALL(*mockClient, getThumbnail(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&firstOnDone))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t,
                                              const std::string&,
                                              std::function<void(Result<std::string>)> onDone) {
            ++depth;
            maxDepth = std::max(maxDepth, depth);
            onDone(Result<std::string>::fail("gone", 2));
            --depth;
        }));

    // maxConcurrent 1 so every request past the first has to go through the
    // queue rather than starting straight away.
    ThumbnailService service(mockClient, 1);
    int finishedCount = 0;

    // Distinct handles, since request() dedupes by handle
    for (std::uint64_t handle = 1; handle <= 50; ++handle)
    {
        service.request(handle, "/tmp/t.jpg", [&](Result<std::string>) {
            ++finishedCount;
        });
    }
    ASSERT_TRUE(static_cast<bool>(firstOnDone));

    // Act: handle 1 finishes, and 2..50 all fail the moment they start
    firstOnDone(Result<std::string>::fail("gone", 2));

    // Assert
    EXPECT_EQ(finishedCount, 50);
    EXPECT_EQ(maxDepth, 1); // recursing would make this 49
}
