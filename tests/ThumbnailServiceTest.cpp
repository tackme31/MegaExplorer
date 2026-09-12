#include "core/ThumbnailService.h"

#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr const char* kCacheRoot = "C:\\cache\\thumbnails";
constexpr const char* kSep = "\\";
constexpr std::uint64_t kAccount = 111;

// What the service is expected to resolve for a handle: <root>\<account>\<handle>.jpg
std::string cachePath(std::uint64_t handle)
{
    return std::string(kCacheRoot) + "\\" + std::to_string(kAccount) + "\\" +
           std::to_string(handle) + ".jpg";
}

std::string leafOf(const std::string& path)
{
    const std::size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The account directory the service resolves for kAccount.
std::string accountDirectory()
{
    return std::string(kCacheRoot) + "\\" + std::to_string(kAccount);
}

class FakeLocalFileSystem : public ILocalFileSystem
{
public:
    // Paths a previous run is pretending to have left behind, and their sizes.
    std::map<std::string, std::uint64_t> files;
    std::set<std::string> createdDirectories;
    std::vector<std::string> removedPaths;
    // Paths removeFile() refuses, standing in for a file locked by another process.
    std::set<std::string> undeletable;

    std::optional<LocalEntry> entryFor(const std::string& path) const override
    {
        if (createdDirectories.count(path) != 0)
        {
            LocalEntry entry;
            entry.path = path;
            entry.name = leafOf(path);
            entry.isDirectory = true;
            return entry;
        }
        auto it = files.find(path);
        if (it == files.end())
            return std::nullopt;
        LocalEntry entry;
        entry.path = path;
        entry.name = leafOf(path);
        entry.sizeBytes = it->second;
        return entry;
    }

    bool createDirectory(const std::string& path) override
    {
        createdDirectories.insert(path);
        return true;
    }

    bool removeFile(const std::string& path) override
    {
        removedPaths.push_back(path);
        if (undeletable.count(path) != 0)
            return false;
        files.erase(path);
        return true;
    }

    std::unique_ptr<ILocalFileWriter> createFile(const std::string&) override
    {
        return nullptr;
    }

    // Only the directory's own files, which is all the cache ever holds. Nullopt for
    // a directory nothing created, so the service can tell "never fetched" apart from
    // "refused to be read".
    std::optional<std::vector<LocalEntry>> listDirectory(const std::string& path) const override
    {
        if (createdDirectories.count(path) == 0)
            return std::nullopt;
        std::vector<LocalEntry> entries;
        for (const auto& file : files)
        {
            const std::size_t slash = file.first.find_last_of('\\');
            if (slash == std::string::npos || file.first.substr(0, slash) != path)
                continue;
            LocalEntry entry;
            entry.path = file.first;
            entry.name = file.first.substr(slash + 1);
            entry.sizeBytes = file.second;
            entries.push_back(entry);
        }
        return entries;
    }

    std::optional<std::string> moveToFreeName(const std::string&, const std::string&) override
    {
        return std::nullopt;
    }
};

std::shared_ptr<::testing::NiceMock<MockMegaClient>> makeClient()
{
    auto client = std::make_shared<::testing::NiceMock<MockMegaClient>>();
    ON_CALL(*client, currentUserHandle())
        .WillByDefault(::testing::Return(Result<std::uint64_t>::ok(kAccount)));
    return client;
}

} // namespace

TEST(ThumbnailServiceTest, InitialRequestCallsSdkAndReturnsResult)
{
    // Arrange
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    EXPECT_CALL(*mockClient, getThumbnail(7, std::string(cachePath(7)), ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    bool called = false;
    Result<std::string> received;

    // Act
    service.request(7, [&](Result<std::string> result) {
        called = true;
        received = std::move(result);
    });

    // Assert
    ASSERT_TRUE(called);
    EXPECT_TRUE(received.success);
    EXPECT_EQ(received.value(), cachePath(7));
}

TEST(ThumbnailServiceTest, SecondRequestForCachedHandleDoesNotCallSdkAgain)
{
    // Arrange
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    EXPECT_CALL(*mockClient, getThumbnail(::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    service.request(7, [](Result<std::string>) {});

    // Act: second request for the same handle, after the first completed
    bool called = false;
    Result<std::string> received;
    service.request(7, [&](Result<std::string> result) {
        called = true;
        received = std::move(result);
    });

    // Assert: served from cache, no second SDK call (enforced by Times(1) above)
    ASSERT_TRUE(called);
    EXPECT_TRUE(received.success);
    EXPECT_EQ(received.value(), cachePath(7));
}

TEST(ThumbnailServiceTest, DuplicateInFlightRequestAttachesCallbackWithoutDuplicateSdkCall)
{
    // Arrange: capture the SDK callback instead of invoking it, so the first
    // request is still in flight when the second one arrives.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::function<void(Result<std::string>)> onDone;
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::SaveArg<2>(&onDone));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    bool firstCalled = false;
    bool secondCalled = false;
    Result<std::string> firstResult;
    Result<std::string> secondResult;

    // Act: two requests for the same handle before the SDK call finishes
    service.request(7, [&](Result<std::string> result) {
        firstCalled = true;
        firstResult = std::move(result);
    });
    service.request(7, [&](Result<std::string> result) {
        secondCalled = true;
        secondResult = std::move(result);
    });

    // Assert: neither callback fired yet, SDK was called exactly once
    ASSERT_FALSE(firstCalled);
    ASSERT_FALSE(secondCalled);
    ASSERT_TRUE(static_cast<bool>(onDone));

    // Act: the single in-flight SDK call finishes
    onDone(Result<std::string>::ok(cachePath(7)));

    // Assert: both callers received the result
    EXPECT_TRUE(firstCalled);
    EXPECT_TRUE(secondCalled);
    EXPECT_TRUE(firstResult.success);
    EXPECT_TRUE(secondResult.success);
    EXPECT_EQ(firstResult.value(), cachePath(7));
    EXPECT_EQ(secondResult.value(), cachePath(7));
}

TEST(ThumbnailServiceTest, RequestsBeyondMaxConcurrentAreQueuedThenAutoStart)
{
    // Arrange: maxConcurrent = 2, so a 3rd distinct handle must wait.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::function<void(Result<std::string>)> onDone1;
    std::function<void(Result<std::string>)> onDone3;

    EXPECT_CALL(*mockClient, getThumbnail(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&onDone1));
    EXPECT_CALL(*mockClient, getThumbnail(2, ::testing::_, ::testing::_));
    EXPECT_CALL(*mockClient, getThumbnail(3, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&onDone3));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot, /*maxConcurrent=*/2);

    // Act: request 3 distinct handles back-to-back
    service.request(1, [](Result<std::string>) {});
    service.request(2, [](Result<std::string>) {});
    service.request(3, [](Result<std::string>) {});

    // Assert: handle 3 hasn't reached the SDK yet -- it's queued behind the
    // maxConcurrent=2 cap
    ASSERT_FALSE(static_cast<bool>(onDone3));

    // Act: handle 1 finishes, freeing a slot
    ASSERT_TRUE(static_cast<bool>(onDone1));
    onDone1(Result<std::string>::ok(cachePath(1)));

    // Assert: handle 3 auto-started
    ASSERT_TRUE(static_cast<bool>(onDone3));
}

TEST(ThumbnailServiceTest, FailedRequestIsNotCachedAndRetriedOnNextRequest)
{
    // Arrange
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::fail("no thumbnail", 2)))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    Result<std::string> firstResult;
    Result<std::string> secondResult;

    // Act
    service.request(7, [&](Result<std::string> result) {
        firstResult = std::move(result);
    });
    service.request(7, [&](Result<std::string> result) {
        secondResult = std::move(result);
    });

    // Assert: first failed and wasn't cached, so the second request hit the
    // SDK again (enforced by the two WillOnce()s above) and succeeded.
    EXPECT_FALSE(firstResult.success);
    EXPECT_EQ(firstResult.errorMessage, "no thumbnail");
    EXPECT_TRUE(secondResult.success);
    EXPECT_EQ(secondResult.value(), cachePath(7));
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
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
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
    ThumbnailService service(mockClient, fileSystem, kCacheRoot, 1);
    int finishedCount = 0;

    // Distinct handles, since request() dedupes by handle
    for (std::uint64_t handle = 1; handle <= 50; ++handle)
    {
        service.request(handle, [&](Result<std::string>) {
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

TEST(ThumbnailServiceTest, AThrowingClientCallLeavesTheQueueAbleToStartTheNextJob)
{
    // Regression guard for the re-entrancy flag's and the active slot's lifetimes:
    // both used to be dropped only at the exits that return normally, so an exception
    // from anything startNextIfCapacity() calls left them set forever and no thumbnail
    // was ever fetched again. maxConcurrent 1 so the leaked slot alone would block it.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::function<void(Result<std::string>)> secondOnDone;
    EXPECT_CALL(*mockClient, getThumbnail(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Throw(std::runtime_error("boom")))
        .WillRepeatedly(::testing::SaveArg<2>(&secondOnDone));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot, 1);
    EXPECT_THROW(service.request(1, [](Result<std::string>) {}), std::runtime_error);

    // Act
    service.request(2, [](Result<std::string>) {});

    // Assert
    EXPECT_TRUE(static_cast<bool>(secondOnDone));
}

TEST(ThumbnailServiceTest, AThrowAfterAnInStackCompletionDoesNotWedgeTheQueue)
{
    // The rollback above must release the slot it took, not whatever entry happens to
    // sit under that handle: a completion running inside the same getThumbnail() call
    // already gave the slot back, and a re-request from its callbacks puts a fresh,
    // unstarted entry there. Decrementing for that one wraps the unsigned count and no
    // thumbnail is ever started again -- the very symptom this cycle removed.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::function<void(Result<std::string>)> secondOnDone;
    int starts = 0;
    EXPECT_CALL(*mockClient, getThumbnail(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t,
                                              const std::string&,
                                              std::function<void(Result<std::string>)> onDone) {
            if (++starts == 1)
            {
                onDone(Result<std::string>::fail("gone", 2)); // finishJob runs in-stack
                throw std::runtime_error("boom");
            }
            secondOnDone = std::move(onDone);
        }));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot, 1);
    bool reRequested = false;
    EXPECT_THROW(service.request(1,
                                 [&](Result<std::string>) {
                                     if (reRequested)
                                         return;
                                     reRequested = true;
                                     // same handle: a failure is not cached, so this makes
                                     // a new, unstarted job entry under handle 1
                                     service.request(1, [](Result<std::string>) {});
                                 }),
                 std::runtime_error);
    ASSERT_TRUE(reRequested);

    // Act
    service.request(2, [](Result<std::string>) {});

    // Assert
    EXPECT_TRUE(static_cast<bool>(secondOnDone));
}

TEST(ThumbnailServiceTest, AFileLeftByAPreviousRunIsServedWithoutCallingTheSdk)
{
    // The whole point of the on-disk cache: a fresh process has nothing in mCache, but
    // the file the last run fetched is still where it left it.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->files[cachePath(7)] = 4096;
    EXPECT_CALL(*mockClient, getThumbnail(::testing::_, ::testing::_, ::testing::_)).Times(0);

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    bool called = false;
    Result<std::string> received;

    // Act
    service.request(7, [&](Result<std::string> result) {
        called = true;
        received = std::move(result);
    });

    // Assert
    ASSERT_TRUE(called);
    EXPECT_TRUE(received.success);
    EXPECT_EQ(received.value(), cachePath(7));
}

TEST(ThumbnailServiceTest, AnEmptyFileFromAnInterruptedFetchIsFetchedAgain)
{
    // Nothing ever revisits a handle the cache answers, so serving a zero-byte file
    // would leave a broken image on that row for good.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->files[cachePath(7)] = 0;
    EXPECT_CALL(*mockClient, getThumbnail(7, cachePath(7), ::testing::_))
        .Times(1)
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    Result<std::string> received;

    // Act
    service.request(7, [&](Result<std::string> result) {
        received = std::move(result);
    });

    // Assert: refetched, into a directory the service made sure exists
    EXPECT_TRUE(received.success);
    EXPECT_THAT(fileSystem->createdDirectories,
                ::testing::Contains(std::string(kCacheRoot) + "\\" + std::to_string(kAccount)));
}

TEST(ThumbnailServiceTest, ASecondAccountDoesNotSeeTheFirstAccountsCachedPath)
{
    // Handles are not documented to be unique across accounts, and logging out does not
    // restart the process, so the in-memory cache has to go when the account changes.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::uint64_t account = kAccount;
    ON_CALL(*mockClient, currentUserHandle()).WillByDefault(::testing::Invoke([&account] {
        return Result<std::uint64_t>::ok(account);
    }));
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::Invoke([](std::uint64_t,
                                             const std::string& path,
                                             std::function<void(Result<std::string>)> onDone) {
            onDone(Result<std::string>::ok(path));
        }));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    Result<std::string> first;
    service.request(7, [&](Result<std::string> result) {
        first = std::move(result);
    });

    // Act: same handle, different account
    account = 222;
    Result<std::string> second;
    service.request(7, [&](Result<std::string> result) {
        second = std::move(result);
    });

    // Assert
    EXPECT_EQ(first.value(), cachePath(7));
    EXPECT_EQ(second.value(),
              std::string(kCacheRoot) + "\\" + std::to_string(account) + "\\" + "7.jpg");
}

TEST(ThumbnailServiceTest, DiscardRemovesTheFileAndMakesTheNextRequestFetchAgain)
{
    // Arrange
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    EXPECT_CALL(*mockClient, getThumbnail(7, std::string(cachePath(7)), ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    service.request(7, [](Result<std::string>) {});
    fileSystem->files[cachePath(7)] = 1024; // what the fetch above left behind

    // Act
    service.discard({7});
    bool refetched = false;
    service.request(7, [&](Result<std::string>) {
        refetched = true;
    });

    // Assert
    EXPECT_THAT(fileSystem->removedPaths, ::testing::ElementsAre(cachePath(7)));
    EXPECT_TRUE(refetched);
}

TEST(ThumbnailServiceTest, DiscardLeavesAHandleWhoseFetchIsStillRunningAlone)
{
    // The SDK is writing that very file, so removing it would delete a fetch in
    // progress -- and what it brings back is fresh anyway.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::function<void(Result<std::string>)> pending;
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&pending](std::uint64_t,
                                               const std::string&,
                                               std::function<void(Result<std::string>)> onDone) {
            pending = std::move(onDone);
        }));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    service.request(7, [](Result<std::string>) {});

    // Act
    service.discard({7});

    // Assert
    EXPECT_TRUE(fileSystem->removedPaths.empty());
    pending(Result<std::string>::ok(cachePath(7)));
}

TEST(ThumbnailServiceTest, DiscardWithNobodySignedInRemovesNothing)
{
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    ON_CALL(*mockClient, currentUserHandle())
        .WillByDefault(::testing::Return(Result<std::uint64_t>::fail("not logged in", -11)));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);

    // Act
    service.discard({7});

    // Assert
    EXPECT_TRUE(fileSystem->removedPaths.empty());
}

TEST(ThumbnailServiceTest, AFileDiscardCouldNotDeleteIsNotServedAsADiskHit)
{
    // The disk hit never calls the SDK, so a file that survived the discard would be
    // served as fresh forever -- and nothing would ever overwrite it either.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->files[cachePath(7)] = 1024;
    fileSystem->undeletable.insert(cachePath(7));
    EXPECT_CALL(*mockClient, getThumbnail(7, std::string(cachePath(7)), ::testing::_))
        .Times(1)
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    service.request(7, [](Result<std::string>) {}); // served from disk, no SDK call

    // Act
    service.discard({7});
    bool refetched = false;
    service.request(7, [&](Result<std::string> result) {
        refetched = result.success;
    });

    // Assert -- the Times(1) above is the real assertion: the file that outlived the
    // discard was fetched over rather than served a second time.
    EXPECT_THAT(fileSystem->removedPaths, ::testing::ElementsAre(cachePath(7)));
    EXPECT_TRUE(refetched);
}

TEST(ThumbnailServiceTest, CachedBytesSumsTheSignedInAccountsFiles)
{
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->createdDirectories.insert(accountDirectory());
    fileSystem->files[cachePath(7)] = 1024;
    fileSystem->files[cachePath(8)] = 2048;
    // Another account's directory, which this one must not be charged for.
    const std::string otherAccount = std::string(kCacheRoot) + kSep + "999";
    fileSystem->createdDirectories.insert(otherAccount);
    fileSystem->files[otherAccount + kSep + "7.jpg"] = 4096;

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);

    const Result<std::uint64_t> size = service.cachedBytes();

    ASSERT_TRUE(size.success);
    EXPECT_EQ(size.value(), 3072u);
}

TEST(ThumbnailServiceTest, CachedBytesIsZeroBeforeAnythingHasBeenFetched)
{
    // The account directory is created on demand, so "not there" is an empty cache
    // rather than a listing that failed.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);

    const Result<std::uint64_t> size = service.cachedBytes();

    ASSERT_TRUE(size.success);
    EXPECT_EQ(size.value(), 0u);
}

TEST(ThumbnailServiceTest, CachedBytesFailsWithNobodySignedIn)
{
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    ON_CALL(*mockClient, currentUserHandle())
        .WillByDefault(::testing::Return(Result<std::uint64_t>::fail("not logged in", -11)));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);

    EXPECT_FALSE(service.cachedBytes().success);
}

TEST(ThumbnailServiceTest, ClearCacheRemovesEveryFileAndTheNextRequestFetchesAgain)
{
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    EXPECT_CALL(*mockClient, getThumbnail(7, std::string(cachePath(7)), ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(Result<std::string>::ok(cachePath(7))));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    service.request(7, [](Result<std::string>) {});
    fileSystem->files[cachePath(7)] = 1024; // what the fetch above left behind
    fileSystem->files[cachePath(8)] = 2048; // and what an earlier run left

    const Result<void> cleared = service.clearCache();
    bool refetched = false;
    service.request(7, [&](Result<std::string>) {
        refetched = true;
    });

    EXPECT_TRUE(cleared.success);
    EXPECT_THAT(fileSystem->removedPaths,
                ::testing::UnorderedElementsAre(cachePath(7), cachePath(8)));
    EXPECT_TRUE(refetched);
}

TEST(ThumbnailServiceTest, ClearCacheLeavesAHandleWhoseFetchIsStillRunningAlone)
{
    // Same reason as discard(): the SDK is writing that very file.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    std::function<void(Result<std::string>)> pending;
    EXPECT_CALL(*mockClient, getThumbnail(7, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&pending](std::uint64_t,
                                               const std::string&,
                                               std::function<void(Result<std::string>)> onDone) {
            pending = std::move(onDone);
        }));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);
    service.request(7, [](Result<std::string>) {});
    fileSystem->files[cachePath(7)] = 1024; // the bytes the pending fetch is writing
    fileSystem->files[cachePath(8)] = 2048;

    EXPECT_TRUE(service.clearCache().success);

    EXPECT_THAT(fileSystem->removedPaths, ::testing::ElementsAre(cachePath(8)));
    pending(Result<std::string>::ok(cachePath(7)));
}

TEST(ThumbnailServiceTest, ClearCacheReportsAFileThatRefusedToGo)
{
    // Silence would read as "emptied" while the bytes are still on disk, and the
    // dialog would then show 0 for a directory that is not empty.
    auto mockClient = makeClient();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->createdDirectories.insert(accountDirectory());
    fileSystem->files[cachePath(7)] = 1024;
    fileSystem->files[cachePath(8)] = 2048;
    fileSystem->undeletable.insert(cachePath(7));

    ThumbnailService service(mockClient, fileSystem, kCacheRoot);

    EXPECT_FALSE(service.clearCache().success);
    const Result<std::uint64_t> size = service.cachedBytes();
    ASSERT_TRUE(size.success);
    EXPECT_EQ(size.value(), 1024u);
}
