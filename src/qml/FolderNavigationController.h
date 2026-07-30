#pragma once
#include "core/FileOperationService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/SortOrder.h"
#include "FileListModel.h"

#include <QObject>
#include <QVariantList>

#include <memory>
#include <string>

class NotificationController;

// Q_INVOKABLE entry points below fire off SDK-thread callbacks that outlive
// any single call; enable_shared_from_this + shared_from_this() captures in
// those callbacks keep `this` alive until the callback runs, even if the
// owning tab is closed mid-fetch (see TabsController.h's lifetime writeup
// for the full rationale -- Phase 9 introduced per-tab instances of this
// controller, so it's no longer guaranteed to outlive every in-flight
// callback the way a single app-lifetime instance was).

// QML-facing GUI glue wrapping FolderNavigationService + SearchService +
// FileListModel. QML can't pass C++ callbacks, so the Q_INVOKABLE entry
// points below are fire-and-forget: internally they hand the service a bound
// lambda, marshal its result onto the GUI thread, then update the owned
// FileListModel and canGoBack. Untested by convention: src/qml is GUI glue,
// and MegaExplorerTests only links MegaExplorerCore.
//
// DownloadController deliberately never touches FileListModel (stays
// decoupled from folder navigation). ThumbnailController is an intentional
// exception to that: it needs to update visible rows in place, so
// fileListModelForThumbnails() below hands it a typed pointer to the same
// FileListModel instance this controller owns.
class FolderNavigationController : public QObject,
                                   public std::enable_shared_from_this<FolderNavigationController>
{
    Q_OBJECT
    Q_PROPERTY(QObject* fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)
    // Elements are QVariantMap{"name", "handle", "isRoot"}, root-first,
    // current folder last -- QML composes display labels itself (e.g. the
    // root's "Cloud Drive" label), C++ only supplies structured fields.
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY breadcrumbChanged)
    // Both derived from mBreadcrumb's last element -- backs TabStrip.qml's
    // tab titles (TabsController relays this controller's breadcrumbChanged
    // into a per-row dataChanged()). atRoot is true (and name empty) before
    // the breadcrumb has ever resolved, so a brand-new tab's title reads as
    // "Cloud Drive" rather than blank.
    Q_PROPERTY(QString currentFolderName READ currentFolderName NOTIFY breadcrumbChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY breadcrumbChanged)
    // Same derivation as currentFolderName/atRoot above -- backs
    // FolderTreePanel.qml's highlight (Phase 10): 0 when the breadcrumb
    // hasn't resolved yet or the current location is the root (meaningless
    // sentinel handle, same convention as PathSegment::isRoot).
    Q_PROPERTY(quint64 currentHandle READ currentHandle NOTIFY breadcrumbChanged)

public:
    explicit FolderNavigationController(std::shared_ptr<FolderNavigationService> navigationService,
                                        std::shared_ptr<SearchService> searchService,
                                        std::shared_ptr<FileOperationService> fileOperationService,
                                        NotificationController* notifications,
                                        QObject* parent = nullptr);

    QObject* fileListModel();

    // Typed accessor to the same FileListModel instance as fileListModel()
    // above, for main.cpp's composition root to hand to ThumbnailController.
    // Not Q_INVOKABLE/not QML-facing -- QML only ever needs the QObject*
    // property for its view's model:.
    FileListModel* fileListModelForThumbnails();

    bool canGoBack() const;

    QVariantList breadcrumb() const;

    QString currentFolderName() const;
    bool atRoot() const;
    quint64 currentHandle() const;

    // Not Q_INVOKABLE: called once from main.cpp's composition root (via
    // AuthController::authStateChanged reaching LoggedIn), not from QML.
    Q_INVOKABLE void loadRoot();

    Q_INVOKABLE void openFolder(quint64 handle);
    Q_INVOKABLE void goBack();

    // Breadcrumb segment click. isRoot mirrors PathSegment::isRoot -- handle
    // is meaningless when it's true (same sentinel convention used
    // throughout, see PathSegment.h).
    Q_INVOKABLE void navigateTo(quint64 handle, bool isRoot);

    // Empty query clears the search and restores the last folder listing
    // (from mLastFolderEntries, no server round-trip). A non-empty query
    // runs a recursive search scoped to the currently open folder; results
    // replace the list but don't touch navigation state (back-stack/
    // canGoBack), so opening a folder from search results still works via
    // the existing openFolder().
    Q_INVOKABLE void search(QString query);

    // column: 0=Name, 1=ModificationTime, 2=Size (FileTableView.qml's 3-column
    // layout). Called both from the header-click handler and once at startup
    // with the Settings-restored value (see mHasLoadedOnce below) -- QML's
    // Component.onCompleted fires before login/loadRoot() have run.
    Q_INVOKABLE void setSortOrder(int column, bool ascending);

    // Clears all navigation/listing state back to a fresh, pre-login state.
    // Called on AuthController::authStateChanged reaching LoggedOut (sign
    // out, or a definitively-invalid restored session) so a subsequent
    // login -- possibly a different account -- never briefly shows the
    // previous account's cached listing or retains its back-stack handles.
    Q_INVOKABLE void reset();

    // Inline-rename commit (F2 / context menu, see InlineRenameField.qml).
    // Callers must skip this when newName equals the current name -- this
    // controller doesn't track per-row names. A rename never changes the
    // handle, so the selection survives the refetch below.
    Q_INVOKABLE void renameEntry(quint64 handle, const QString& newName);

    // Moves every currently selected entry into the Rubbish bin, one SDK call
    // per entry, and reports the tally once through
    // NotificationController::notifyOperation. No undo: IMegaClient
    // deliberately exposes no general move, so there'd be nothing to undo
    // with (see docs/PROGRESS.md's Phase 12 log).
    Q_INVOKABLE void moveSelectionToRubbish();

signals:
    void canGoBackChanged();
    void breadcrumbChanged();

private:
    void applyResult(Result<std::vector<FileEntry>> result);
    void applySearchResult(Result<std::vector<FileEntry>> result);
    void refreshCurrentFolder();

    // "Re-fetch whatever the user is currently looking at" -- the folder
    // listing, or the active search's results (plus, in that case, the cached
    // folder listing behind them, so clearing the search afterwards doesn't
    // show a stale one). Shared by setSortOrder and by the Phase 12 mutations,
    // which must not silently drop the user out of a search.
    void refreshVisibleListing();

    // Resolves the current location's ancestor chain and updates
    // mBreadcrumb. Called at the end of applyResult's success path (loadRoot
    // / openFolder / goBack / navigateTo / refreshCurrent all funnel through
    // it), so the breadcrumb tracks the actual folder hierarchy rather than
    // navigation history. Only emits breadcrumbChanged if the resolved path
    // actually differs, so refreshCurrent (e.g. a sort-order change) doesn't
    // needlessly rebuild the Breadcrumb.qml Repeater.
    void refreshBreadcrumb();

    // Shared bookkeeping for one moveSelectionToRubbish() fan-out: only the
    // last callback to land refreshes the listing and reports the tally, so
    // N deletions produce one refetch and one notification. Same shape as
    // QuickAccessModel's Sweep.
    struct RubbishBatch
    {
        int remaining = 0;
        int succeeded = 0;
        int failed = 0;
    };

    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<SearchService> mSearchService;
    std::shared_ptr<FileOperationService> mFileOps;
    NotificationController* mNotifications;
    FileListModel mFileListModel;
    QVariantList mBreadcrumb;
    std::vector<FileEntry> mLastFolderEntries; // restored when search is cleared
    SortOrder mSortOrder{SortKey::Name, true};
    std::string mLastSearchQuery; // empty == not currently searching
    // loadRoot() is called only after engine.loadFromModule() has already
    // run QML's Component.onCompleted, which restores the persisted sort via
    // setSortOrder() -- guards against that startup call re-fetching (and
    // erroring out) before login/fetchNodes have ever run. Set true once
    // applyResult sees its first success; reset back to false by reset().
    bool mHasLoadedOnce = false;
};
