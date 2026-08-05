#pragma once
#include <QAbstractListModel>
#include <QObject>

#include <functional>
#include <memory>
#include <vector>

class FolderNavigationService;
class SearchService;
class FolderNavigationController;
class ThumbnailController;
class UploadController;

// Everything one tab needs to be self-contained: its own navigation/search
// scope (back-stack, current folder, last search query) and its own
// FileListModel-backed thumbnail cache-writer. ThumbnailService itself
// (handle-keyed disk/network cache) and DownloadService/AuthService/
// NotificationController all stay app-lifetime singletons, shared across
// every tab -- only the pieces that are inherently per-navigation-scope are
// duplicated here. Deliberately no QObject parent on navigation/thumbnails:
// TabContext's shared_ptr members are the sole owners, so a tab's
// controllers are destroyed exactly when the last shared_ptr to them (here,
// or in a still-running async callback's shared_from_this() capture) goes
// away -- see FolderNavigationController.h's lifetime comment.
struct TabContext
{
    std::shared_ptr<FolderNavigationService> navigationService;
    std::shared_ptr<SearchService> searchService;
    std::shared_ptr<FolderNavigationController> navigation;
    std::shared_ptr<ThumbnailController> thumbnails;
};

// QAbstractListModel doubling as both the tab strip's model (TabStrip.qml)
// and the app's tab-management command surface, registered as the
// "tabsController" context property (main.cpp) in place of the old
// single-instance "controller"/"thumbnailController" properties. A plain
// QVariantList (Breadcrumb.qml's approach) won't do here: it has no row-level
// diffing, so every emit rebuilds every delegate -- fatal for tabs, whose
// whole point is that each pane (and its scroll position/selection/focus)
// stays alive across a tab switch. QAbstractListModel's begin/end
// insert/removeRows lets qml/Main.qml's Repeater add/remove exactly the
// panes that changed.
//
// Tab construction is delegated to a factory the composition root
// (main.cpp) supplies: only main.cpp knows how to wire a
// FolderNavigationService/SearchService/FolderNavigationController/
// ThumbnailController together (shared client/thumbnailService/
// notifications), and tabs are created dynamically (unlike every other
// controller, which main.cpp can construct once up front) -- see
// docs/ARCHITECTURE.md's composition-root convention.
class TabsController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentTabChanged)
    // QML-facing QObject* so the header (Back button/Breadcrumb/search field)
    // can bind through tabsController.currentNavigation instead of a single
    // app-lifetime "controller" context property. Null only in the
    // (unreachable in practice -- see closeTab) empty-tabs state.
    Q_PROPERTY(QObject* currentNavigation READ currentNavigation NOTIFY currentTabChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles
    {
        TitleRole = Qt::UserRole + 1, // raw currentFolderName(), "" at root
        AtRootRole,                   // true if this tab is showing the root
        NavigationRole,               // FolderNavigationController* (QObject*)
        ThumbnailsRole,               // ThumbnailController* (QObject*)
        BusyRole,                     // that tab has an operation in flight
    };

    // uploads is app-global and non-owning (main.cpp outlives this): it's
    // folded into BusyRole rather than into the per-tab controller because an
    // upload belongs to no tab -- see the BusyRole case in data().
    explicit TabsController(std::function<TabContext()> factory,
                            UploadController* uploads,
                            QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;
    void setCurrentIndex(int index);
    QObject* currentNavigation() const;
    int count() const;

    // Opens a brand-new tab at the cloud drive root and switches to it (the
    // "+" button).
    Q_INVOKABLE void addTab();

    // Opens a brand-new tab at handle (isRoot mirrors
    // FolderNavigationController::navigateTo's sentinel convention) in the
    // background, without switching to it -- middle-click on a folder, or
    // the context menu's "Open in new tab" (Explorer/browser convention: the
    // current tab stays focused).
    Q_INVOKABLE void addTabAt(quint64 handle, bool isRoot);

    // Closes the tab at index. Clamps currentIndex so it keeps pointing at
    // the same tab (if it moved) or a sensible neighbor (if the active tab
    // itself was closed). Emits lastTabClosed() instead when index was the
    // last remaining tab -- Main.qml closes the window in response, rather
    // than this class reaching for QGuiApplication itself.
    Q_INVOKABLE void closeTab(int index);

    // Drag reordering (Phase 22b). `to` is the row the tab should end up on,
    // not an insertion point in the pre-move coordinates -- same convention
    // as QuickAccessModel::move. currentIndex follows the tab it was pointing
    // at, so reordering never switches tabs. Out-of-range or no-op moves emit
    // nothing.
    Q_INVOKABLE void moveTab(int from, int to);

    // Collapses every tab but the first back down to one and re-fetches the
    // root in it -- called once on AuthController::authStateChanged reaching
    // LoggedIn (replaces the old single-controller "controller.loadRoot()").
    Q_INVOKABLE void loadRootAll();

    // Collapses every tab but the first back down to one and resets it to a
    // fresh pre-login state -- called on AuthController::authStateChanged
    // reaching LoggedOut (replaces the old "controller.reset()"), so a
    // subsequent login (possibly a different account) starts from a single
    // empty tab rather than retaining however many tabs/folders the previous
    // session had open.
    Q_INVOKABLE void resetAll();

signals:
    void currentTabChanged();
    void countChanged();

    // Main.qml listens for this and calls window.close() -- this class only
    // manages the tab list, it doesn't own the window.
    void lastTabClosed();

private:
    TabContext createTab();
    // Removes every tab but the first (creating one first if there somehow
    // are none), resets currentIndex to 0. Shared tail of loadRootAll/
    // resetAll -- the two differ only in which method they then call on the
    // surviving tab's navigation controller.
    void collapseToSingleTab();

    // Emits dataChanged for whichever row currently holds navigation. Looked
    // up by pointer rather than a captured index: tabs can be inserted or
    // removed after the connection that calls this was made, which would
    // otherwise leave a stale row number behind. No-op if the tab is gone.
    void emitRowChangedFor(const FolderNavigationController* navigation, const QList<int>& roles);

    std::function<TabContext()> mFactory;
    UploadController* mUploads;
    std::vector<TabContext> mTabs;
    int mCurrentIndex = 0;
};
