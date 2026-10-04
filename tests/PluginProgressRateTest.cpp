#include "qml/PluginProgressRate.h"

#include <gtest/gtest.h>

TEST(PluginProgressRateTest, NeedsTwoIncreasesApartInTime)
{
    PluginProgressRate rate;
    EXPECT_FALSE(rate.perSecond().has_value());
    rate.add(1000, 0);
    EXPECT_FALSE(rate.perSecond().has_value());
    rate.add(1000, 5);
    EXPECT_FALSE(rate.perSecond().has_value());
    rate.add(1500, 10);
    ASSERT_TRUE(rate.perSecond().has_value());
    EXPECT_DOUBLE_EQ(*rate.perSecond(), 20.0);
    EXPECT_EQ(rate.lastIncreaseAt(), 1500);
}

TEST(PluginProgressRateTest, FollowsOnlyTheLastTenIncreases)
{
    PluginProgressRate rate;
    // A slow start: 1 item per second for 20 items...
    for (int i = 0; i <= 20; ++i)
        rate.add(i * 1000, i);
    EXPECT_DOUBLE_EQ(*rate.perSecond(), 1.0);
    // ...then 10 items per second. Ten increases later the start no longer counts.
    for (int i = 1; i <= 10; ++i)
        rate.add(20000 + i * 100, 20 + i);
    EXPECT_DOUBLE_EQ(*rate.perSecond(), 10.0);
}

TEST(PluginProgressRateTest, IgnoresARepeatedCountAndRestartsWhenTheCountGoesBack)
{
    PluginProgressRate rate;
    rate.add(0, 0);
    rate.add(1000, 10);
    // A message-only update repeats the count: it is not a stall to average in.
    rate.add(5000, 10);
    EXPECT_DOUBLE_EQ(*rate.perSecond(), 10.0);
    EXPECT_EQ(rate.lastIncreaseAt(), 1000);

    rate.add(6000, 0);
    EXPECT_FALSE(rate.perSecond().has_value());
    rate.add(7000, 4);
    EXPECT_DOUBLE_EQ(*rate.perSecond(), 4.0);

    rate.add(8000, -1);
    EXPECT_FALSE(rate.perSecond().has_value());
}
