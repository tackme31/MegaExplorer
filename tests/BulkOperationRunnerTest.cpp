#include "qml/BulkOperationRunner.h"

#include "qml/BusyState.h"
#include "qml/NotificationController.h"

#include <QEventLoop>
#include <QTimer>

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

// The runner's own logic is synchronous; only the busy state it drives has a
// delay timer, so these two helpers are the same ones BusyStateTest needs and
// for the same reason. Deliberately not QSignalSpy: Qt6::Test isn't linked.
constexpr int kBusyWaitTimeoutMs = 2000;

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

class BulkOperationRunnerTest : public ::testing::Test
{
protected:
    BulkOperationRunnerTest()
        : runner(busy, notifications, [this]() {
              ++defaultRefreshes;
              order.push_back("defaultRefresh");
          })
    {
        QObject::connect(&notifications,
                         &NotificationController::operationFinished,
                         [this](QString context, int succeeded, int failed) {
                             ++notifications_seen;
                             lastContext = context;
                             lastSucceeded = succeeded;
                             lastFailed = failed;
                             order.push_back("notify");
                         });
    }

    static Result<void> failure()
    {
        return Result<void>::fail("no", -9);
    }

    BusyState busy;
    NotificationController notifications;
    BulkOperationRunner runner;

    int defaultRefreshes = 0;
    int notifications_seen = 0;
    QString lastContext;
    int lastSucceeded = 0;
    int lastFailed = 0;
    std::vector<std::string> order;
};

TEST_F(BulkOperationRunnerTest, LastSettleRefreshesAndNotifiesOnce)
{
    auto batch = runner.start("move", 3);

    batch->settle(Result<void>::ok());
    batch->settle(Result<void>::ok());
    batch->settle(Result<void>::ok());

    EXPECT_EQ(defaultRefreshes, 1);
    EXPECT_EQ(notifications_seen, 1);
    EXPECT_EQ(lastContext, QStringLiteral("move"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
}

TEST_F(BulkOperationRunnerTest, NothingHappensBeforeTheLastSettle)
{
    auto batch = runner.start("move", 3);

    batch->settle(Result<void>::ok());
    batch->settle(failure());

    EXPECT_EQ(defaultRefreshes, 0);
    EXPECT_EQ(notifications_seen, 0);
}

TEST_F(BulkOperationRunnerTest, SeparatesSucceededFromFailed)
{
    int completedSucceeded = -1;
    int completedFailed = -1;
    auto batch = runner.start("moveToRubbish", 3, {}, [&](int succeeded, int failed) {
        completedSucceeded = succeeded;
        completedFailed = failed;
    });

    batch->settle(Result<void>::ok());
    batch->settle(failure());
    batch->settle(failure());

    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 2);
    EXPECT_EQ(completedSucceeded, 1);
    EXPECT_EQ(completedFailed, 2);
}

TEST_F(BulkOperationRunnerTest, CustomRefreshReplacesTheDefault)
{
    int custom = 0;
    auto batch = runner.start("copy", 2, [&custom]() {
        ++custom;
    });

    batch->settle(Result<void>::ok());
    batch->settle(Result<void>::ok());

    EXPECT_EQ(custom, 1);
    EXPECT_EQ(defaultRefreshes, 0);
}

TEST_F(BulkOperationRunnerTest, OnCompleteRunsAfterTheRefreshAndTheNotification)
{
    auto batch = runner.start("copy", 1, {}, [this](int, int) {
        order.push_back("onComplete");
    });

    batch->settle(Result<void>::ok());

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "defaultRefresh");
    EXPECT_EQ(order[1], "notify");
    EXPECT_EQ(order[2], "onComplete");
}

// The pairing the runner exists to make checkable: start() takes N begins,
// each settle() gives back exactly one end, so the spinner survives the whole
// fan-out and clears on the last callback -- not the first.
TEST_F(BulkOperationRunnerTest, BusyStaysUpUntilEveryOperationHasSettled)
{
    auto batch = runner.start("move", 3);
    ASSERT_TRUE(waitForVisible(busy, true));

    batch->settle(Result<void>::ok());
    batch->settle(Result<void>::ok());
    EXPECT_TRUE(busy.visible());

    batch->settle(Result<void>::ok());
    EXPECT_FALSE(busy.visible());
}

} // namespace
