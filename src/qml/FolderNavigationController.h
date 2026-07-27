#pragma once
#include "core/FileListingService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "FileListModel.h"

#include <QObject>

#include <memory>
#include <string>

class NotificationController;

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
class FolderNavigationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)

public:
    explicit FolderNavigationController(std::shared_ptr<FolderNavigationService> navigationService,
                                        std::shared_ptr<FileListingService> listingService,
                                        std::shared_ptr<SearchService> searchService,
                                        NotificationController* notifications,
                                        QObject* parent = nullptr);

    QObject* fileListModel();

    // Typed accessor to the same FileListModel instance as fileListModel()
    // above, for main.cpp's composition root to hand to ThumbnailController.
    // Not Q_INVOKABLE/not QML-facing -- QML only ever needs the QObject*
    // property for its view's model:.
    FileListModel* fileListModelForThumbnails();

    bool canGoBack() const;

    // Not Q_INVOKABLE: called once from main.cpp's composition root before
    // app.exec(), not from QML.
    void loadRoot(const std::string& email, const std::string& password);

    Q_INVOKABLE void openFolder(quint64 handle);
    Q_INVOKABLE void goBack();

    // Empty query clears the search and restores the last folder listing
    // (from mLastFolderEntries, no server round-trip). A non-empty query
    // runs a recursive search scoped to the currently open folder; results
    // replace the list but don't touch navigation state (back-stack/
    // canGoBack), so opening a folder from search results still works via
    // the existing openFolder().
    Q_INVOKABLE void search(QString query);

signals:
    void canGoBackChanged();

private:
    void applyResult(Result<std::vector<FileEntry>> result);
    void applySearchResult(Result<std::vector<FileEntry>> result);

    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<FileListingService> mListingService;
    std::shared_ptr<SearchService> mSearchService;
    NotificationController* mNotifications;
    FileListModel mFileListModel;
    std::vector<FileEntry> mLastFolderEntries; // restored when search is cleared
};
