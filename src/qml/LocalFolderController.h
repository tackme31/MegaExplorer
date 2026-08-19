#pragma once
#include "core/LocalLinkService.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class NotificationController;

// The one local folder that stands in for the MEGA root, plus the "show this node
// in Explorer" action built on it.
//
// The path is owned and persisted by Main.qml's Settings item and pushed in here,
// the same division the colour-scheme preference uses -- this class resolves and
// opens, and decides nothing about where the value is kept.
class LocalFolderController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localRoot READ localRoot WRITE setLocalRoot NOTIFY localRootChanged)
    // Whether a folder is linked at all. The right-click menu drops the action
    // outright when it isn't, rather than greying it: with no folder there is
    // nothing for the entry to name.
    Q_PROPERTY(bool linked READ linked NOTIFY localRootChanged)

public:
    explicit LocalFolderController(std::shared_ptr<LocalLinkService> service,
                                   NotificationController* notifications,
                                   QObject* parent = nullptr);

    QString localRoot() const;
    void setLocalRoot(QString path);
    bool linked() const;

    // FolderDialog hands back a file: URL, and QML has no toLocalFile of its own;
    // string surgery on the URL text gets UNC paths and percent-escapes wrong.
    Q_INVOKABLE QString pathFromUrl(const QUrl& url) const;

    // Reveals the node's local counterpart in Explorer with the item selected.
    // Every failure -- nothing linked, node gone, nothing at the joined path --
    // is the same toast, since the answer is always to revisit the setting.
    Q_INVOKABLE void openLocation(quint64 handle);

signals:
    void localRootChanged();

private:
    std::shared_ptr<LocalLinkService> mService;
    NotificationController* mNotifications;
    QString mLocalRoot;
};
