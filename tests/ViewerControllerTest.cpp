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

TEST(ViewerControllerTest, ViewableNamesAreImagesOnly)
{
    Fixture f;
    EXPECT_TRUE(f.controller.canView("photo.JPG"));
    EXPECT_TRUE(f.controller.canView("raw.cr2"));
    // Both have their own viewer still to come; until then double-click leaves them
    // inert rather than opening them in the image viewer.
    EXPECT_FALSE(f.controller.canView("clip.mp4"));
    EXPECT_FALSE(f.controller.canView("manual.pdf"));
    EXPECT_FALSE(f.controller.canView("notes.txt"));
    EXPECT_FALSE(f.controller.canView("README"));
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
