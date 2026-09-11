#include "core/PreviewService.h"

#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

TEST(PreviewServiceTest, SingleRequestReachesClientAndDelivers)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPreview(7, std::string("/tmp/7-1.jpg"), ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok("/tmp/7-1.jpg")));

    PreviewService service(mockClient);
    Result<std::string> received;
    bool called = false;

    // Act
    service.request(7, "/tmp/7-1.jpg", [&](Result<std::string> result) {
        called = true;
        received = std::move(result);
    });

    // Assert
    ASSERT_TRUE(called);
    EXPECT_TRUE(received.success);
    EXPECT_EQ(received.value(), "/tmp/7-1.jpg");
}

TEST(PreviewServiceTest, SecondRequestWaitsWhileTheFirstIsInFlight)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> firstOnDone;
    EXPECT_CALL(*mockClient, getPreview(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::SaveArg<2>(&firstOnDone));

    PreviewService service(mockClient);
    bool secondFinished = false;

    // Act
    service.request(1, "/tmp/1-1.jpg", [](Result<std::string>) {});
    service.request(2, "/tmp/2-2.jpg", [&](Result<std::string>) {
        secondFinished = true;
    });

    // Assert: the second never started, and nothing has finished it either
    ASSERT_TRUE(static_cast<bool>(firstOnDone));
    EXPECT_FALSE(secondFinished);
}

TEST(PreviewServiceTest, ThirdRequestEvictsTheWaitingOneWithSupersededCode)
{
    // The whole point of the one-deep waiting slot: holding Down across a folder
    // must not queue a fetch per row. Only the newest waiting request survives, and
    // the evicted one is finished immediately so its caller can drop its state.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::vector<std::uint64_t> requested;
    std::function<void(Result<std::string>)> firstOnDone;
    EXPECT_CALL(*mockClient, getPreview(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t handle,
                                              const std::string&,
                                              std::function<void(Result<std::string>)> onDone) {
            requested.push_back(handle);
            if (requested.size() == 1)
                firstOnDone = std::move(onDone);
        }));

    PreviewService service(mockClient);
    Result<std::string> secondResult;
    bool secondFinished = false;
    bool thirdFinished = false;

    // Act
    service.request(1, "/tmp/1-1.jpg", [](Result<std::string>) {});
    service.request(2, "/tmp/2-2.jpg", [&](Result<std::string> result) {
        secondFinished = true;
        secondResult = std::move(result);
    });
    service.request(3, "/tmp/3-3.jpg", [&](Result<std::string>) {
        thirdFinished = true;
    });

    // Assert: 2 was evicted by 3 before ever reaching the client
    ASSERT_TRUE(secondFinished);
    EXPECT_FALSE(secondResult.success);
    EXPECT_EQ(secondResult.errorCode, kPreviewSuperseded);
    EXPECT_FALSE(thirdFinished);
    EXPECT_THAT(requested, ::testing::ElementsAre(1));

    // Act: the in-flight one finishes, so the survivor starts -- not the evicted one
    ASSERT_TRUE(static_cast<bool>(firstOnDone));
    firstOnDone(Result<std::string>::ok("/tmp/1-1.jpg"));

    // Assert
    EXPECT_THAT(requested, ::testing::ElementsAre(1, 3));
}

TEST(PreviewServiceTest, SameHandleTwiceReachesTheClientTwice)
{
    // The deliberate contrast with ThumbnailService, which would serve the second
    // one from its cache. Previews are never cached: the pane must not show a
    // document that has since changed.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPreview(7, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(Result<std::string>::ok("/tmp/7.jpg")));

    PreviewService service(mockClient);

    service.request(7, "/tmp/7-1.jpg", [](Result<std::string>) {});
    service.request(7, "/tmp/7-2.jpg", [](Result<std::string>) {});
}

TEST(PreviewServiceTest, ChainedRequestsFromCallbacksDoNotRecurse)
{
    // Regression guard for startNextIfIdle()'s re-entrancy trampoline. Two things
    // stack here at once: getPreview fails in-stack when the handle no longer
    // resolves (IMegaClient.h's delivery mode 3), and PreviewController's own
    // callback issues the next request as the cursor keeps moving. Without the
    // trampoline that is one nested frame per row.
    auto mockClient = std::make_shared<MockMegaClient>();
    int depth = 0;
    int maxDepth = 0;
    EXPECT_CALL(*mockClient, getPreview(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t,
                                              const std::string&,
                                              std::function<void(Result<std::string>)> onDone) {
            ++depth;
            maxDepth = std::max(maxDepth, depth);
            onDone(Result<std::string>::fail("gone", 2));
            --depth;
        }));

    PreviewService service(mockClient);
    int finishedCount = 0;
    std::function<void(Result<std::string>)> onDone;
    onDone = [&](Result<std::string>) {
        ++finishedCount;
        if (finishedCount < 50)
            service.request(static_cast<std::uint64_t>(finishedCount) + 1, "/tmp/p.jpg", onDone);
    };

    // Act
    service.request(1, "/tmp/p.jpg", onDone);

    // Assert
    EXPECT_EQ(finishedCount, 50);
    EXPECT_EQ(maxDepth, 1); // recursing would make this 50
}

namespace
{
using ChunkFn = std::function<bool(const char*, std::size_t)>;
using StreamDoneFn = std::function<void(Result<void>)>;
} // namespace

TEST(PreviewServiceTest, RangeReadReassemblesThePiecesItIsHanded)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, readFileRangeStreamed(7, 100, 6, ::testing::_, ::testing::_))
        .WillOnce(
            [](std::uint64_t, std::uint64_t, std::uint64_t, ChunkFn onChunk, StreamDoneFn onDone) {
                EXPECT_TRUE(onChunk("abc", 3));
                EXPECT_TRUE(onChunk("def", 3));
                onDone(Result<void>::ok());
            });

    PreviewService service(mockClient);
    Result<std::vector<char>> received;
    service.requestRange(7, 100, 6, [&](Result<std::vector<char>> result) {
        received = std::move(result);
    });

    ASSERT_TRUE(received.success);
    EXPECT_EQ(std::string(received.value().begin(), received.value().end()), "abcdef");
}

TEST(PreviewServiceTest, CancelStopsAnInFlightRangeReadAndFreesTheSlotAtOnce)
{
    // Arrange: a range read in flight, and an image waiting behind it
    auto mockClient = std::make_shared<MockMegaClient>();
    ChunkFn onChunk;
    StreamDoneFn rangeDone;
    EXPECT_CALL(*mockClient,
                readFileRangeStreamed(7, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            [&](std::uint64_t, std::uint64_t, std::uint64_t, ChunkFn chunk, StreamDoneFn done) {
                onChunk = std::move(chunk);
                rangeDone = std::move(done);
            });
    std::function<void(Result<std::string>)> imageDone;
    EXPECT_CALL(*mockClient, getPreview(8, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&imageDone));

    PreviewService service(mockClient);
    int rangeReports = 0;
    Result<std::vector<char>> rangeResult;
    service.requestRange(7, 0, 1000, [&](Result<std::vector<char>> result) {
        ++rangeReports;
        rangeResult = std::move(result);
    });
    ASSERT_TRUE(static_cast<bool>(onChunk));
    EXPECT_TRUE(onChunk("abc", 3));

    // Act
    service.cancel();

    // Assert: finished as superseded without waiting for the transfer, which refuses
    // its next piece
    ASSERT_EQ(rangeReports, 1);
    EXPECT_EQ(rangeResult.errorCode, kPreviewSuperseded);
    EXPECT_FALSE(onChunk("def", 3));

    // Act: the next request starts at once, though the old transfer has not ended
    service.request(8, "/tmp/8.jpg", [](Result<std::string>) {});
    ASSERT_TRUE(static_cast<bool>(imageDone));

    // Act: the old transfer's own end arrives late
    rangeDone(Result<void>::fail("Stopped by the receiver", -13));

    // Assert: dropped -- not reported twice, and it did not free the image's slot
    EXPECT_EQ(rangeReports, 1);
    EXPECT_CALL(*mockClient, getPreview(9, ::testing::_, ::testing::_)).Times(0);
    service.request(9, "/tmp/9.jpg", [](Result<std::string>) {});
}

TEST(PreviewServiceTest, CancelLeavesAStartedImageFetchHoldingTheSlot)
{
    // getPreview has no way to be stopped, so cancelling only drops what waits behind it.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> firstDone;
    std::vector<std::uint64_t> requested;
    EXPECT_CALL(*mockClient, getPreview(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly([&](std::uint64_t handle,
                            const std::string&,
                            std::function<void(Result<std::string>)> onDone) {
            requested.push_back(handle);
            if (requested.size() == 1)
                firstDone = std::move(onDone);
        });

    PreviewService service(mockClient);
    bool firstReported = false;
    Result<std::string> waitingResult;
    service.request(1, "/tmp/1.jpg", [&](Result<std::string>) {
        firstReported = true;
    });
    service.request(2, "/tmp/2.jpg", [&](Result<std::string> result) {
        waitingResult = std::move(result);
    });

    // Act
    service.cancel();
    service.request(3, "/tmp/3.jpg", [](Result<std::string>) {});

    // Assert
    EXPECT_FALSE(firstReported);
    EXPECT_EQ(waitingResult.errorCode, kPreviewSuperseded);
    EXPECT_THAT(requested, ::testing::ElementsAre(1));
    ASSERT_TRUE(static_cast<bool>(firstDone));
    firstDone(Result<std::string>::ok("/tmp/1.jpg"));
    EXPECT_TRUE(firstReported);
    EXPECT_THAT(requested, ::testing::ElementsAre(1, 3));
}

TEST(PreviewServiceTest, AThrowingClientCallLeavesTheServiceAbleToStartTheNextRequest)
{
    // Regression guard for the re-entrancy flag's and the in-flight slot's lifetimes:
    // both used to be dropped only at the exits that return normally, so an exception
    // from anything startNextIfIdle() calls left them set forever and no preview was
    // ever fetched again.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> secondOnDone;
    EXPECT_CALL(*mockClient, getPreview(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Throw(std::runtime_error("boom")))
        .WillRepeatedly(::testing::SaveArg<2>(&secondOnDone));

    PreviewService service(mockClient);
    EXPECT_THROW(service.request(1, "/tmp/1.jpg", [](Result<std::string>) {}), std::runtime_error);

    // Act
    service.request(2, "/tmp/2.jpg", [](Result<std::string>) {});

    // Assert
    EXPECT_TRUE(static_cast<bool>(secondOnDone));
}
