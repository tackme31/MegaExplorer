#include "qml/TabsController.h"

#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "MockMegaClient.h"
#include "qml/FolderNavigationController.h"
#include "qml/NotificationController.h"
#include "qml/ThumbnailController.h"

#include <gtest/gtest.h>

// TabsController is src/qml GUI glue, which this codebase otherwise leaves
// untested by convention (see FolderNavigationController.h's own comment) --
// deliberately bent here, same rationale as FileListModel: this is pure
// bookkeeping (row/currentIndex accounting) with real bug potential, not
// view/rendering glue. What's exercised is exactly that bookkeeping; no
// assertion here depends on a navigation fetch actually completing --
// MockMegaClient has no EXPECT_CALL set up, so
// loadRoot()/openFolder()/reset() below just fire an unanswered mock call
// and return, same as a real fetch that's still in flight when the test
// finishes.
namespace
{

// One MockMegaClient shared by every tab a given TabsController creates,
// mirroring main.cpp's real wiring (single IMegaClient, per-tab
// FolderNavigationService/SearchService). NotificationController is
// similarly shared (non-owning pointer), matching production.
class TabsControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        client = std::make_shared<MockMegaClient>();
    }

    TabContext makeTabContext()
    {
        auto navigationService = std::make_shared<FolderNavigationService>(client);
        auto searchService = std::make_shared<SearchService>(client, navigationService);
        auto fileOperationService = std::make_shared<FileOperationService>(client);
        auto navigation = std::make_shared<FolderNavigationController>(
            navigationService, searchService, fileOperationService, &notifications);
        auto thumbnailService = std::make_shared<ThumbnailService>(client);
        auto thumbnails = std::make_shared<ThumbnailController>(
            thumbnailService, navigation->fileListModelForThumbnails(), &notifications);
        return TabContext{std::move(navigationService),
                          std::move(searchService),
                          std::move(navigation),
                          std::move(thumbnails)};
    }

    std::unique_ptr<TabsController> makeController()
    {
        return std::make_unique<TabsController>([this]() {
            return makeTabContext();
        });
    }

    std::shared_ptr<MockMegaClient> client;
    NotificationController notifications;
};

} // namespace

TEST_F(TabsControllerTest, StartsWithExactlyOneTab)
{
    auto tabs = makeController();
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(tabs->rowCount(), 1);
    EXPECT_EQ(tabs->currentIndex(), 0);
    EXPECT_NE(tabs->currentNavigation(), nullptr);
}

TEST_F(TabsControllerTest, AddTabIncreasesCountAndSwitchesToNewTab)
{
    auto tabs = makeController();
    QObject* firstNavigation = tabs->currentNavigation();

    tabs->addTab();

    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->rowCount(), 2);
    EXPECT_EQ(tabs->currentIndex(), 1);
    EXPECT_NE(tabs->currentNavigation(), firstNavigation);
}

TEST_F(TabsControllerTest, AddTabAtIncreasesCountWithoutSwitchingFocus)
{
    auto tabs = makeController();

    tabs->addTabAt(42, false);

    // addTabAt opens the new tab in the background (middle-click/"Open in
    // new tab" semantics) -- unlike addTab()'s "+" button, it deliberately
    // leaves currentIndex pointing at the still-focused original tab.
    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->currentIndex(), 0);
}

TEST_F(TabsControllerTest, ClosingTheOnlyTabEmitsLastTabClosed)
{
    auto tabs = makeController();
    int lastTabClosedCount = 0;
    QObject::connect(tabs.get(), &TabsController::lastTabClosed, [&lastTabClosedCount]() {
        ++lastTabClosedCount;
    });

    tabs->closeTab(0);

    EXPECT_EQ(lastTabClosedCount, 1);
    EXPECT_EQ(tabs->count(), 0);
    EXPECT_EQ(tabs->currentIndex(), -1);
    EXPECT_EQ(tabs->currentNavigation(), nullptr);
}

TEST_F(TabsControllerTest, ClosingTheActiveLastTabClampsCurrentIndexToNewLastTab)
{
    auto tabs = makeController();
    tabs->addTab(); // index 1
    tabs->addTab(); // index 2, active
    ASSERT_EQ(tabs->currentIndex(), 2);

    tabs->closeTab(2);

    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->currentIndex(), 1);
}

TEST_F(TabsControllerTest, ClosingATabBeforeTheActiveOneShiftsCurrentIndexDown)
{
    auto tabs = makeController();
    tabs->addTab(); // index 1
    tabs->addTab(); // index 2, active
    ASSERT_EQ(tabs->currentIndex(), 2);
    QObject* activeNavigation = tabs->currentNavigation();

    tabs->closeTab(0);

    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->currentIndex(), 1);
    // Same tab is still active, just shifted down one row.
    EXPECT_EQ(tabs->currentNavigation(), activeNavigation);
}

TEST_F(TabsControllerTest, ClosingATabAfterTheActiveOneLeavesCurrentIndexUnchanged)
{
    auto tabs = makeController();
    tabs->addTab(); // index 1
    tabs->addTab(); // index 2
    tabs->setCurrentIndex(0);
    QObject* activeNavigation = tabs->currentNavigation();

    tabs->closeTab(2);

    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->currentIndex(), 0);
    EXPECT_EQ(tabs->currentNavigation(), activeNavigation);
}

TEST_F(TabsControllerTest, ClosingAnOutOfRangeIndexIsANoOp)
{
    auto tabs = makeController();

    tabs->closeTab(5);
    tabs->closeTab(-1);

    EXPECT_EQ(tabs->count(), 1);
}

TEST_F(TabsControllerTest, LoadRootAllCollapsesToOneTab)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    ASSERT_EQ(tabs->count(), 3);

    tabs->loadRootAll();

    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(tabs->currentIndex(), 0);
}

TEST_F(TabsControllerTest, ResetAllCollapsesToOneTab)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    ASSERT_EQ(tabs->count(), 3);

    tabs->resetAll();

    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(tabs->currentIndex(), 0);
}

TEST_F(TabsControllerTest, SetCurrentIndexClampsToValidRange)
{
    auto tabs = makeController();
    tabs->addTab();
    ASSERT_EQ(tabs->count(), 2);

    tabs->setCurrentIndex(99);
    EXPECT_EQ(tabs->currentIndex(), 1);

    tabs->setCurrentIndex(-5);
    EXPECT_EQ(tabs->currentIndex(), 0);
}

TEST_F(TabsControllerTest, CountChangedFiresOnAddAndClose)
{
    auto tabs = makeController();
    int emissions = 0;
    QObject::connect(tabs.get(), &TabsController::countChanged, [&emissions]() {
        ++emissions;
    });

    tabs->addTab();
    tabs->closeTab(0);

    EXPECT_EQ(emissions, 2);
}
