#pragma once
#include <QAbstractListModel>
#include <QObject>

#include <functional>
#include <memory>
#include <vector>

class FolderNavigationService;
class SearchService;
class FileMutationController;
class FolderNavigationController;
class ThumbnailController;
class UploadController;

// Everything one tab needs to be self-contained: its own navigation/search scope
// and its own FileListModel-backed thumbnail cache-writer. ThumbnailService,
// DownloadService, AuthService and NotificationController stay app-lifetime
// singletons -- only what is inherently per-navigation-scope is duplicated here.
//
// Deliberately no QObject parent on the controllers: these shared_ptr members are
// the sole owners, so a tab's controllers die exactly when the last reference does
// -- possibly one held by a still-running async callback.
struct TabContext
{
    std::shared_ptr<FolderNavigationService> navigationService;
    std::shared_ptr<SearchService> searchService;
    std::shared_ptr<FolderNavigationController> navigation;
    // Holds a shared_ptr to navigation above, so it can only outlive it, never
    // the other way round -- see FileMutationController.h.
    std::shared_ptr<FileMutationController> mutations;
    std::shared_ptr<ThumbnailController> thumbnails;
};

// The tab strip's model and the app's tab-management command surface. A plain
// QVariantList won't do: it has no row-level diffing, so every emit rebuilds every
// delegate -- fatal for tabs, whose whole point is that each pane keeps its scroll
// position, selection and focus across a switch.
//
// Tab construction is delegated to a factory the composition root supplies: only
// it knows how to wire a tab's services and controllers together, and tabs are
// created dynamically rather than once up front.
class TabsController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentTabChanged)
    // What the header (Back button, breadcrumb, search field) binds through. Null
    // only in the empty-tabs state, which closeTab makes unreachable in practice.
    Q_PROPERTY(QObject* currentNavigation READ currentNavigation NOTIFY currentTabChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles
    {
        TitleRole = Qt::UserRole + 1, // raw currentFolderName(), "" at root
        AtRootRole,                   // true if this tab is showing the root
        NavigationRole,               // FolderNavigationController* (QObject*)
        MutationsRole,                // FileMutationController* (QObject*)
        ThumbnailsRole,               // ThumbnailController* (QObject*)
        BusyRole,                     // that tab has an operation in flight
        ViewKindRole,                 // ViewKind of the screen this tab shows
    };

    // uploads is app-global and non-owning (the composition root outlives this). It
    // is folded into BusyRole rather than into a per-tab controller because an
    // upload belongs to no tab.
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

    Q_INVOKABLE void addTab();

    // Opens the tab in the background without switching to it -- middle-click and
    // "Open in new tab" leave the current tab focused, as in Explorer and browsers.
    Q_INVOKABLE void addTabAt(quint64 handle, bool isRoot);

    // Same background-tab semantics, with no handle to take: the favourites
    // listing is a query, not a folder.
    Q_INVOKABLE void addFavouritesTab();

    // Same again for the Rubbish bin.
    Q_INVOKABLE void addRubbishTab();

    // Clamps currentIndex so it keeps pointing at the same tab, or a neighbour if
    // the active one was closed. Emits lastTabClosed() instead of closing the final
    // tab -- Main.qml closes the window, this class doesn't own it.
    Q_INVOKABLE void closeTab(int index);

    // `to` is the row the tab should end up on, not an insertion point in pre-move
    // coordinates (same as QuickAccessModel::move). currentIndex follows the tab it
    // pointed at, so reordering never switches tabs.
    Q_INVOKABLE void moveTab(int from, int to);

    // Collapses to one tab and re-fetches the root in it; called when auth reaches
    // LoggedIn.
    Q_INVOKABLE void loadRootAll();

    // The LoggedOut counterpart, so a subsequent login starts from one empty tab
    // rather than the previous session's tabs and folders.
    Q_INVOKABLE void resetAll();

signals:
    void currentTabChanged();
    void countChanged();

    void lastTabClosed();

private:
    TabContext createTab();
    // Shared tail of loadRootAll/resetAll; creates a tab first if there somehow are
    // none.
    void collapseToSingleTab();

    // Looked up by pointer rather than a captured index: tabs can be inserted or
    // removed after the connection was made, leaving a stale row number behind.
    void emitRowChangedFor(const FolderNavigationController* navigation, const QList<int>& roles);

    // Pays off a stale mark on whichever tab is on screen -- after a fan-out, and
    // whenever the current tab changes.
    void refreshCurrentTabIfStale();

    std::function<TabContext()> mFactory;
    UploadController* mUploads;
    std::vector<TabContext> mTabs;
    int mCurrentIndex = 0;
};
