#include "qml/ViewerController.h"

#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace
{

struct Fixture
{
    std::shared_ptr<MockMegaClient> client = std::make_shared<MockMegaClient>();
    ViewerController controller{client};
};

} // namespace

TEST(ViewerControllerTest, ViewerKindNamesTheViewerThatOpensTheFile)
{
    Fixture f;
    EXPECT_EQ(f.controller.viewerKind("photo.JPG"), QStringLiteral("image"));
    EXPECT_EQ(f.controller.viewerKind("raw.cr2"), QStringLiteral("image"));
    EXPECT_EQ(f.controller.viewerKind("clip.MP4"), QStringLiteral("video"));
    EXPECT_EQ(f.controller.viewerKind("clip.mkv"), QStringLiteral("video"));
    // PDF has its own viewer still to come; until then double-click leaves it inert
    // rather than opening it in a viewer that cannot show it.
    EXPECT_TRUE(f.controller.viewerKind("manual.pdf").isEmpty());
    EXPECT_TRUE(f.controller.viewerKind("notes.txt").isEmpty());
    EXPECT_TRUE(f.controller.viewerKind("README").isEmpty());
}

TEST(ViewerControllerTest, SourceUrlPassesTheClientsUrlThrough)
{
    Fixture f;
    EXPECT_CALL(*f.client, streamingUrl(42u))
        .WillOnce(testing::Return(Result<std::string>::ok("http://127.0.0.1:8080/2a/photo.jpg")));

    EXPECT_EQ(f.controller.sourceUrl(42u), QStringLiteral("http://127.0.0.1:8080/2a/photo.jpg"));
}

TEST(ViewerControllerTest, SourceUrlIsEmptyWhenTheServerWillNotStart)
{
    Fixture f;
    EXPECT_CALL(*f.client, streamingUrl(42u))
        .WillOnce(testing::Return(Result<std::string>::fail("server not started", -1)));

    EXPECT_TRUE(f.controller.sourceUrl(42u).isEmpty());
}
