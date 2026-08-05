#include "qml/TabsController.h"

#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "MockMegaClient.h"
#include "qml/ClipboardController.h"
#include "qml/FolderNavigationController.h"
#include "qml/NotificationController.h"
#include "qml/ThumbnailController.h"
#include "qml/UploadController.h"

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
        // Real one rather than a stub: TabsController only reads
        // isUploadingTo() off it, and with nothing ever enqueued that answer is
        // a constant false -- what's under test here is row bookkeeping.
        uploads = std::make_unique<UploadController>(std::make_shared<UploadService>(client),
                                                     std::make_shared<FileOperationService>(client),
                                                     &notifications);
    }

    TabContext makeTabContext()
    {
        auto navigationService = std::make_shared<FolderNavigationService>(client);
        auto searchService = std::make_shared<SearchService>(client, navigationService);
        auto fileOperationService = std::make_shared<FileOperationService>(client);
        auto navigation = std::make_shared<FolderNavigationController>(
            navigationService, searchService, fileOperationService, &notifications, &clipboard);
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
        return std::make_unique<TabsController>(
            [this]() {
                return makeTabContext();
            },
            uploads.get());
    }

    std::shared_ptr<MockMegaClient> client;
    NotificationController notifications;
    // Shared by every tab, like main.cpp's -- and never filled here, so no
    // assertion below depends on it.
    ClipboardController clipboard;
    std::unique_ptr<UploadController> uploads;
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

TEST_F(TabsControllerTest, MoveTabReordersRight)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    QObject* first = tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>();
    QObject* second = tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>();
    QObject* third = tabs->data(tabs->index(2), TabsController::NavigationRole).value<QObject*>();

    tabs->moveTab(0, 2);

    EXPECT_EQ(tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>(), second);
    EXPECT_EQ(tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>(), third);
    EXPECT_EQ(tabs->data(tabs->index(2), TabsController::NavigationRole).value<QObject*>(), first);
    EXPECT_EQ(tabs->count(), 3);
}

TEST_F(TabsControllerTest, MoveTabReordersLeft)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    QObject* first = tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>();
    QObject* second = tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>();
    QObject* third = tabs->data(tabs->index(2), TabsController::NavigationRole).value<QObject*>();

    tabs->moveTab(2, 0);

    EXPECT_EQ(tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>(), third);
    EXPECT_EQ(tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>(), first);
    EXPECT_EQ(tabs->data(tabs->index(2), TabsController::NavigationRole).value<QObject*>(), second);
}

TEST_F(TabsControllerTest, MoveTabEmitsRowsMovedExactlyOnce)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    int emissions = 0;
    QObject::connect(tabs.get(), &TabsController::rowsMoved, [&emissions]() {
        ++emissions;
    });

    tabs->moveTab(0, 2);

    EXPECT_EQ(emissions, 1);
}

TEST_F(TabsControllerTest, MoveTabRejectsNoOpAndOutOfRange)
{
    auto tabs = makeController();
    tabs->addTab();
    QObject* first = tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>();
    int emissions = 0;
    QObject::connect(tabs.get(), &TabsController::rowsMoved, [&emissions]() {
        ++emissions;
    });

    tabs->moveTab(0, 0);
    tabs->moveTab(-1, 1);
    tabs->moveTab(0, 5);
    tabs->moveTab(5, 0);

    EXPECT_EQ(emissions, 0);
    EXPECT_EQ(tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>(), first);
}

TEST_F(TabsControllerTest, MovingTheActiveTabCarriesCurrentIndexWithIt)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab(); // index 2, active
    ASSERT_EQ(tabs->currentIndex(), 2);
    QObject* activeNavigation = tabs->currentNavigation();

    tabs->moveTab(2, 0);

    EXPECT_EQ(tabs->currentIndex(), 0);
    EXPECT_EQ(tabs->currentNavigation(), activeNavigation);
}

TEST_F(TabsControllerTest, MovingAnotherTabAcrossTheActiveOneShiftsCurrentIndex)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    tabs->setCurrentIndex(1);
    QObject* activeNavigation = tabs->currentNavigation();

    // Dragging tab 0 to the right past the active tab slides the active tab
    // one slot to the left; its identity is unchanged.
    tabs->moveTab(0, 2);
    EXPECT_EQ(tabs->currentIndex(), 0);
    EXPECT_EQ(tabs->currentNavigation(), activeNavigation);

    // And back the other way.
    tabs->moveTab(2, 0);
    EXPECT_EQ(tabs->currentIndex(), 1);
    EXPECT_EQ(tabs->currentNavigation(), activeNavigation);
}

TEST_F(TabsControllerTest, MoveEntirelyBesideTheActiveTabLeavesCurrentIndexAlone)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    tabs->addTab(); // four tabs, index 3 active
    tabs->setCurrentIndex(0);
    int currentTabChangedCount = 0;
    QObject::connect(tabs.get(), &TabsController::currentTabChanged, [&currentTabChangedCount]() {
        ++currentTabChangedCount;
    });

    tabs->moveTab(1, 3);

    EXPECT_EQ(tabs->currentIndex(), 0);
    EXPECT_EQ(currentTabChangedCount, 0);
}

TEST_F(TabsControllerTest, MoveTabDoesNotEmitCountChanged)
{
    auto tabs = makeController();
    tabs->addTab();
    int emissions = 0;
    QObject::connect(tabs.get(), &TabsController::countChanged, [&emissions]() {
        ++emissions;
    });

    tabs->moveTab(0, 1);

    EXPECT_EQ(emissions, 0);
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
