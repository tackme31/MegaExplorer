#include "core/LocalLinkService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <set>

namespace
{

// Only entryFor matters here; listDirectory is never reached, so it answers
// "could not list".
class FakeLocalFileSystem : public ILocalFileSystem
{
public:
    void add(const std::string& path)
    {
        mPaths.insert(path);
    }

    std::optional<LocalEntry> entryFor(const std::string& path) const override
    {
        if (mPaths.find(path) == mPaths.end())
            return std::nullopt;
        LocalEntry entry;
        entry.path = path;
        return entry;
    }

    std::optional<std::vector<LocalEntry>> listDirectory(const std::string&) const override
    {
        return std::nullopt;
    }

private:
    std::set<std::string> mPaths;
};

// The shape MegaSdkClient::getPath hands back: root first, the node itself last.
std::vector<PathSegment> pathOf(const std::vector<std::string>& names)
{
    std::vector<PathSegment> segments;
    segments.push_back(PathSegment{"Cloud Drive", 0, true, ViewKind::CloudDrive});
    for (const std::string& name : names)
        segments.push_back(PathSegment{name, 7, false, ViewKind::CloudDrive});
    return segments;
}

struct Captured
{
    bool called = false;
    Result<std::string> result;
};

std::function<void(Result<std::string>)> captureInto(Captured& captured)
{
    return [&captured](Result<std::string> result) {
        captured.called = true;
        captured.result = std::move(result);
    };
}

} // namespace

TEST(LocalLinkServiceTest, JoinsTheMegaPathBelowTheRootOntoTheLinkedFolder)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->add("C:\\Mega\\photos\\a.jpg");

    EXPECT_CALL(*mockClient, getPath(42u, false, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::ok(pathOf({"photos", "a.jpg"}))));

    LocalLinkService service(mockClient, fileSystem);
    Captured captured;

    service.resolveLocalPath(42, "C:\\Mega", captureInto(captured));

    ASSERT_TRUE(captured.called);
    ASSERT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value(), "C:\\Mega\\photos\\a.jpg");
}

TEST(LocalLinkServiceTest, IgnoresATrailingSeparatorOnTheLinkedFolder)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();
    fileSystem->add("C:\\Mega\\a.jpg");

    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({"a.jpg"}))));

    LocalLinkService service(mockClient, fileSystem);

    // Only a trailing separator is normalized -- the root itself arrives native,
    // since LocalFolderController::pathFromUrl converts before it is stored.
    for (const std::string& root : {"C:\\Mega\\", "C:\\Mega/"})
    {
        Captured captured;
        service.resolveLocalPath(42, root, captureInto(captured));
        ASSERT_TRUE(captured.called);
        ASSERT_TRUE(captured.result.success) << root;
        EXPECT_EQ(captured.result.value(), "C:\\Mega\\a.jpg");
    }
}

TEST(LocalLinkServiceTest, FailsWhenNothingExistsAtTheJoinedPath)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();

    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::ok(pathOf({"photos", "a.jpg"}))));

    LocalLinkService service(mockClient, fileSystem);
    Captured captured;

    service.resolveLocalPath(42, "C:\\Mega", captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kENoEnt);
}

TEST(LocalLinkServiceTest, AsksTheClientNothingWhenNoFolderIsLinked)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();

    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_)).Times(0);

    LocalLinkService service(mockClient, fileSystem);
    Captured captured;

    service.resolveLocalPath(42, "", captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEArgs);
}

TEST(LocalLinkServiceTest, PassesAFailedPathLookupStraightThrough)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    auto fileSystem = std::make_shared<FakeLocalFileSystem>();

    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::fail("gone", MegaErrorCode::kEAccess)));

    LocalLinkService service(mockClient, fileSystem);
    Captured captured;

    service.resolveLocalPath(42, "C:\\Mega", captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEAccess);
}
