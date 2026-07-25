#pragma once
#include "core/FileListingService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "FileListModel.h"

#include <QObject>

#include <memory>
#include <string>

// QML-facing GUI glue wrapping FolderNavigationService + SearchService +
// FileListModel. QML can't pass C++ callbacks, so the Q_INVOKABLE entry
// points below are fire-and-forget: internally they hand the service a bound
// lambda, marshal its result onto the GUI thread, then update the owned
// FileListModel and canGoBack. Untested by convention: src/qml is GUI glue,
// and MegaExplorerTests only links MegaExplorerCore.
class FolderNavigationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)

public:
    explicit FolderNavigationController(std::shared_ptr<FolderNavigationService> navigationService,
                                         std::shared_ptr<FileListingService> listingService,
                                         std::shared_ptr<SearchService> searchService,
                                         QObject* parent = nullptr);

    QObject* fileListModel();
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
    FileListModel mFileListModel;
    std::vector<FileEntry> mLastFolderEntries; // restored when search is cleared
};
