#include "qml/BusyState.h"

#include <QEventLoop>
#include <QTimer>

#include <gtest/gtest.h>

namespace
{

// BusyState only publishes visible() once its own delay timer has fired, so
// these tests have to let real time pass. Deliberately not QSignalSpy: that
// lives in Qt6::Test, which this target doesn't link (see
// FolderNavigationControllerTest's note on the same choice).
constexpr int kBusyWaitTimeoutMs = 2000;

// Comfortably past BusyState.cpp's private 250ms delay -- how long to wait to
// prove a spinner *doesn't* appear.
constexpr int kPastDelayMs = 750;

bool waitForVisible(BusyState& busy, bool expected)
{
    if (busy.visible() == expected)
        return true;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&busy, &BusyState::changed, &loop, [&busy, expected, &loop]() {
        if (busy.visible() == expected)
            loop.quit();
    });
    timeout.start(kBusyWaitTimeoutMs);
    loop.exec();
    return busy.visible() == expected;
}

void spinFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

class BusyStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QObject::connect(&busy, &BusyState::changed, [this]() {
            ++changes;
        });
    }

    BusyState busy;
    int changes = 0;
};

TEST_F(BusyStateTest, DoesNotShowBeforeTheDelay)
{
    busy.begin();

    EXPECT_FALSE(busy.visible());
    EXPECT_EQ(changes, 0);
}

TEST_F(BusyStateTest, ShowsAfterTheDelayWhileStillInFlight)
{
    busy.begin();

    ASSERT_TRUE(waitForVisible(busy, true));
    EXPECT_EQ(changes, 1);
}

// The reason the delay exists: most operations are server round-trips that land
// well inside it and must never flash a spinner.
TEST_F(BusyStateTest, EndBeforeTheDelayNeverShows)
{
    busy.begin();
    busy.end();

    spinFor(kPastDelayMs);

    EXPECT_FALSE(busy.visible());
    EXPECT_EQ(changes, 0);
}

TEST_F(BusyStateTest, NestedOperationsShowUntilTheLastEnds)
{
    busy.begin();
    busy.begin();
    ASSERT_TRUE(waitForVisible(busy, true));

    busy.end();
    EXPECT_TRUE(busy.visible());

    busy.end();
    EXPECT_FALSE(busy.visible());
}

TEST_F(BusyStateTest, AbandonAllHidesAndLateEndsAreClamped)
{
    busy.begin();
    busy.begin();
    ASSERT_TRUE(waitForVisible(busy, true));

    busy.abandonAll();
    EXPECT_FALSE(busy.visible());
    EXPECT_EQ(changes, 2);

    // The abandoned operations' callbacks still land. They must not drive the
    // count negative, or no later begin() would ever reach 1 again.
    busy.end();
    busy.end();

    busy.begin();
    EXPECT_TRUE(waitForVisible(busy, true));
}

} // namespace
