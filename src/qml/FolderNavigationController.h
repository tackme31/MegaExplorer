#pragma once
#include "core/FolderNavigationService.h"
#include "FileListModel.h"

#include <QObject>

#include <memory>

// QML-facing GUI glue wrapping FolderNavigationService + FileListModel.
// QML can't pass C++ callbacks, so the Q_INVOKABLE entry points below are
// fire-and-forget: internally they hand the service a bound lambda, marshal
// its result onto the GUI thread, then update the owned FileListModel and
// canGoBack. Untested by convention: src/qml is GUI glue, and
// MegaExplorerTests only links MegaExplorerCore.
class FolderNavigationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)

public:
    explicit FolderNavigationController(std::shared_ptr<FolderNavigationService> service,
                                         QObject* parent = nullptr);

    QObject* fileListModel();
    bool canGoBack() const;

    Q_INVOKABLE void openFolder(quint64 handle);
    Q_INVOKABLE void goBack();

signals:
    void canGoBackChanged();

private:
    void applyResult(Result<std::vector<FileEntry>> result);

    std::shared_ptr<FolderNavigationService> mService;
    FileListModel mFileListModel;
};
