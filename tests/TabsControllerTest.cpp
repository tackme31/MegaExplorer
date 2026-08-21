#include "qml/TabsController.h"

#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "core/UploadScanService.h"
#include "MockMegaClient.h"
#include "platform/QtLocalFileSystem.h"
#include "qml/BusyState.h"
#include "qml/ClipboardController.h"
#include "qml/FileMutationController.h"
#include "qml/FolderNavigationController.h"
#include "qml/GuiThread.h"
#include "qml/NotificationController.h"
#include "qml/ThumbnailController.h"
#include "qml/UploadController.h"
#include "qml/ViewKindEnum.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeArgument;

// Except where a test sets its own expectation, no assertion here depends on a
// navigation fetch completing -- MockMegaClient has none set up, so
// loadRoot()/openFolder()/reset() below just fire an unanswered mock call and
// return, same as a real fetch still in flight when the test finishes.
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
        uploads = std::make_unique<UploadController>(
            std::make_shared<UploadService>(client),
            std::make_shared<UploadScanService>(client, std::make_shared<QtLocalFileSystem>()),
            &notifications);
    }

    TabContext makeTabContext()
    {
        auto navigationService = std::make_shared<FolderNavigationService>(client);
        auto searchService = std::make_shared<SearchService>(client, navigationService);
        auto fileOperationService = std::make_shared<FileOperationService>(client);
        // makeGuiOwned like main.cpp: these tests are single-threaded so it
        // always takes the plain-delete branch, but the wiring stays the same
        // shape as production.
        auto busy = makeGuiOwned<BusyState>();
        auto navigation = makeGuiOwned<FolderNavigationController>(
            navigationService, searchService, busy, &notifications);
        auto mutations = makeGuiOwned<FileMutationController>(
            navigation, navigationService, fileOperationService, busy, &notifications, &clipboard);
        auto thumbnailService = std::make_shared<ThumbnailService>(client);
        auto thumbnails = makeGuiOwned<ThumbnailController>(
            thumbnailService, navigation->fileListModelForThumbnails(), &notifications);
        return TabContext{std::move(navigationService),
                          std::move(searchService),
                          std::move(navigation),
                          std::move(mutations),
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

    // The tab's own mutation controller, i.e. the one whose signals only reach
    // the *other* tabs -- which is the half of the fan-out worth asserting on.
    static FileMutationController* mutationsOf(TabsController& tabs, int row)
    {
        return qobject_cast<FileMutationController*>(
            tabs.data(tabs.index(row), TabsController::MutationsRole).value<QObject*>());
    }

    std::shared_ptr<MockMegaClient> client;
    // A favourites listing is a query, so counting the calls is how "re-read
    // exactly once" gets asserted (same device as FolderNavigationControllerTest).
    int favouriteFetches = 0;
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

TEST_F(TabsControllerTest, AddFavouritesTabOpensInBackground)
{
    auto tabs = makeController();

    tabs->addFavouritesTab();

    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->currentIndex(), 0);
}

TEST_F(TabsControllerTest, AddFavouritesTabSwitchesTheNewTabAndNotTheCurrentOne)
{
    EXPECT_CALL(*client, listFavourites(_, _, _, _))
        .WillRepeatedly(Invoke([](SortOrder,
                                  const std::string&,
                                  const SearchFilter&,
                                  std::function<void(Result<std::vector<FileEntry>>)> onDone) {
            onDone(Result<std::vector<FileEntry>>::ok({}));
        }));
    auto tabs = makeController();

    tabs->addFavouritesTab();
    // Two drains: the listing's queued invoke runs applyResult, which posts the
    // breadcrumb resolution as a second one -- and the breadcrumb is what
    // publishes the view kind.
    flushQueuedEvents();
    flushQueuedEvents();

    EXPECT_EQ(tabs->data(tabs->index(1), TabsController::ViewKindRole).toInt(),
              ViewKindEnum::Favourites);
    EXPECT_EQ(tabs->data(tabs->index(0), TabsController::ViewKindRole).toInt(),
              ViewKindEnum::CloudDrive);
}

// The fan-out these three exercise had no coverage at all before F7b: every
// assertion about a cross-tab refresh lived on the emitting side.
TEST_F(TabsControllerTest, AFavouriteToggledElsewhereRefreshesTheFavouritesTabWhenItIsLookedAt)
{
    EXPECT_CALL(*client, listFavourites(_, _, _, _))
        .WillRepeatedly(Invoke([this](SortOrder,
                                      const std::string&,
                                      const SearchFilter&,
                                      std::function<void(Result<std::vector<FileEntry>>)> onDone) {
            ++favouriteFetches;
            onDone(Result<std::vector<FileEntry>>::ok({}));
        }));
    EXPECT_CALL(*client, setNodeFavourite(9u, true, _))
        .WillRepeatedly(InvokeArgument<2>(Result<void>::ok()));
    auto tabs = makeController();
    tabs->addFavouritesTab();
    flushQueuedEvents();
    flushQueuedEvents();
    ASSERT_EQ(favouriteFetches, 1);

    mutationsOf(*tabs, 0)->setEntryFavourite(9, true);
    flushQueuedEvents();

    // Deferred on purpose: one full-drive search per toggle, in every favourites
    // tab open, is the refresh storm the stale mark exists to avoid (spec 5.3).
    EXPECT_EQ(favouriteFetches, 1);

    tabs->setCurrentIndex(1);
    flushQueuedEvents();

    EXPECT_EQ(favouriteFetches, 2);
}

TEST_F(TabsControllerTest, ARubbishMoveElsewhereAlsoLeavesTheFavouritesTabStale)
{
    // A favourites listing matches no folder handle, so the ordinary fan-out
    // passes it by -- yet a node moved to the rubbish bin drops out of it. That
    // combination is guaranteed to happen: this app forbids deleting from the
    // favourites screen, so the deletion is always in some other tab.
    EXPECT_CALL(*client, listFavourites(_, _, _, _))
        .WillRepeatedly(Invoke([this](SortOrder,
                                      const std::string&,
                                      const SearchFilter&,
                                      std::function<void(Result<std::vector<FileEntry>>)> onDone) {
            ++favouriteFetches;
            onDone(Result<std::vector<FileEntry>>::ok({}));
        }));
    auto tabs = makeController();
    tabs->addFavouritesTab();
    flushQueuedEvents();
    flushQueuedEvents();
    ASSERT_EQ(favouriteFetches, 1);

    // The real path, not a hand-emitted signal: until F7b this batch announced
    // nothing at all, so the wiring under test starts at moveHandlesToRubbish.
    EXPECT_CALL(*client, moveToRubbish(7u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    mutationsOf(*tabs, 0)->moveHandlesToRubbish(QVariantList{QVariant::fromValue(quint64{7})});
    flushQueuedEvents();
    EXPECT_EQ(favouriteFetches, 1);

    tabs->setCurrentIndex(1);
    flushQueuedEvents();

    EXPECT_EQ(favouriteFetches, 2);
}

TEST_F(TabsControllerTest, AFavouritesTabAlreadyOnScreenRefreshesWithoutATabSwitch)
{
    EXPECT_CALL(*client, listFavourites(_, _, _, _))
        .WillRepeatedly(Invoke([this](SortOrder,
                                      const std::string&,
                                      const SearchFilter&,
                                      std::function<void(Result<std::vector<FileEntry>>)> onDone) {
            ++favouriteFetches;
            onDone(Result<std::vector<FileEntry>>::ok({}));
        }));
    EXPECT_CALL(*client, setNodeFavourite(9u, false, _))
        .WillRepeatedly(InvokeArgument<2>(Result<void>::ok()));
    auto tabs = makeController();
    tabs->addFavouritesTab();
    tabs->setCurrentIndex(1);
    flushQueuedEvents();
    flushQueuedEvents();
    const int fetchesBefore = favouriteFetches;

    mutationsOf(*tabs, 0)->setEntryFavourite(9, false);
    flushQueuedEvents();

    // Deferring here would leave a stale listing in front of the user until they
    // switched away and back.
    EXPECT_EQ(favouriteFetches - fetchesBefore, 1);
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

TEST_F(TabsControllerTest, DuplicateTabInsertsToTheRightAndFocusesTheCopy)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab();
    QObject* first = tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>();
    QObject* second = tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>();

    tabs->duplicateTab(0);

    EXPECT_EQ(tabs->count(), 4);
    // A copy, not the same tab twice: each tab owns its navigation scope.
    EXPECT_EQ(tabs->data(tabs->index(0), TabsController::NavigationRole).value<QObject*>(), first);
    EXPECT_NE(tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>(), first);
    EXPECT_EQ(tabs->data(tabs->index(2), TabsController::NavigationRole).value<QObject*>(), second);
    EXPECT_EQ(tabs->currentIndex(), 1);
    EXPECT_EQ(tabs->currentNavigation(),
              tabs->data(tabs->index(1), TabsController::NavigationRole).value<QObject*>());
}

TEST_F(TabsControllerTest, DuplicatingBeforeTheActiveTabStillLeavesItAddressable)
{
    auto tabs = makeController();
    tabs->addTab();
    tabs->addTab(); // index 2, active
    QObject* wasActive = tabs->currentNavigation();

    tabs->duplicateTab(0);

    // The previously active tab slid one row right; the copy takes the focus.
    EXPECT_EQ(tabs->data(tabs->index(3), TabsController::NavigationRole).value<QObject*>(),
              wasActive);
    EXPECT_EQ(tabs->currentIndex(), 1);
}

TEST_F(TabsControllerTest, DuplicateTabCopiesTheScreenAndNotJustTheFolder)
{
    EXPECT_CALL(*client, listFavourites(_, _, _, _))
        .WillRepeatedly(Invoke([](SortOrder,
                                  const std::string&,
                                  const SearchFilter&,
                                  std::function<void(Result<std::vector<FileEntry>>)> onDone) {
            onDone(Result<std::vector<FileEntry>>::ok({}));
        }));
    auto tabs = makeController();
    tabs->addFavouritesTab();
    flushQueuedEvents();
    flushQueuedEvents();
    ASSERT_EQ(tabs->data(tabs->index(1), TabsController::ViewKindRole).toInt(),
              ViewKindEnum::Favourites);

    tabs->duplicateTab(1);
    flushQueuedEvents();
    flushQueuedEvents();

    EXPECT_EQ(tabs->data(tabs->index(2), TabsController::ViewKindRole).toInt(),
              ViewKindEnum::Favourites);
}

TEST_F(TabsControllerTest, DuplicatingAnOutOfRangeIndexIsANoOp)
{
    auto tabs = makeController();
    tabs->addTab();

    tabs->duplicateTab(-1);
    tabs->duplicateTab(2);

    EXPECT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->currentIndex(), 1);
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
