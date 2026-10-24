#include "qml/NodeCacheController.h"

#include "MockMegaClient.h"

#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>
#include <memory>

using ::testing::StrictMock;

TEST(NodeCacheControllerTest, PassesAPositiveLimitThrough)
{
    auto client = std::make_shared<StrictMock<MockMegaClient>>();
    NodeCacheController controller(client);
    EXPECT_CALL(*client, setNodeCacheLimit(50000u));

    controller.setLimit(50000);

    EXPECT_EQ(controller.limit(), 50000);
}

TEST(NodeCacheControllerTest, ZeroOrLessMeansNoLimit)
{
    auto client = std::make_shared<StrictMock<MockMegaClient>>();
    NodeCacheController controller(client);
    EXPECT_CALL(*client, setNodeCacheLimit(std::numeric_limits<std::uint64_t>::max())).Times(2);

    controller.setLimit(0);
    controller.setLimit(-5);
}

// The first push from Settings can equal the initial value, and must still reach the
// SDK, whose own default is no limit.
TEST(NodeCacheControllerTest, AppliesEvenWhenUnchangedButSignalsOnlyOnChange)
{
    auto client = std::make_shared<StrictMock<MockMegaClient>>();
    NodeCacheController controller(client);
    int changed = 0;
    QObject::connect(&controller, &NodeCacheController::limitChanged, [&changed] { ++changed; });
    EXPECT_CALL(*client, setNodeCacheLimit(10000u)).Times(2);

    controller.setLimit(10000);
    controller.setLimit(10000);

    EXPECT_EQ(changed, 1);
}
