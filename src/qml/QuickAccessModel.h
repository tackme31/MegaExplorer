#pragma once
#include "core/QuickAccessService.h"

#include <QAbstractListModel>
#include <QString>

#include <cstdint>
#include <memory>

// QML-facing model behind QuickAccessSection.qml's pin list. Flat, unlike
// FolderTreeModel: a pin is a shortcut, never expandable.
//
// App-lifetime singleton owned by main.cpp (a stack local, exposed via
// setContextProperty), shared across every tab like FolderTreeModel -- so,
// same as that class and unlike FolderNavigationController/ThumbnailController,
// it does NOT use enable_shared_from_this and a bare `this` capture in async
// callbacks is safe. invokeOnGuiThread targets qApp for the same reason.
//
// Everything is keyed by handle rather than row index, following
// FileListModel's Phase 13a convention: the login-time validation sweep drops
// dangling pins, which shifts every row after them.
class QuickAccessModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        HandleRole,
    };

    explicit QuickAccessModel(std::shared_ptr<QuickAccessService> service,
                              QObject* parent = nullptr);
    ~QuickAccessModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    // Login: reads the persisted pins, shows them immediately, then starts an
    // async validation sweep (see validateAll) that follows renames and drops
    // pins whose target is gone. Showing the stored names first means no blank
    // panel while the sweep runs.
    Q_INVOKABLE void reload();

    // Sign-out: empties the list without touching the store.
    Q_INVOKABLE void reset();

    Q_INVOKABLE bool isPinned(quint64 handle) const;
    Q_INVOKABLE void pin(quint64 handle, const QString& name);
    Q_INVOKABLE void unpin(quint64 handle);

    // Checks the target still exists before navigating, then reports back via
    // activated() or missing(). Covers the pin going stale *during* a session
    // (deleted on another device), which the login-time sweep can't catch.
    // Navigation itself stays QML's job -- this model has no idea which tab is
    // active.
    Q_INVOKABLE void activate(quint64 handle, bool inNewTab);

signals:
    void countChanged();
    void activated(quint64 handle, bool inNewTab);
    void missing(quint64 handle, QString name);

private:
    void validateAll();
    int rowFor(quint64 handle) const;
    QString nameFor(quint64 handle) const;

    std::shared_ptr<QuickAccessService> mService;

    // Bumped by reload()/reset(); every async callback captures its value and
    // drops its result if it no longer matches. Same guard as
    // FolderTreeModel's, and needed for the same reason: a sign-out or
    // re-login part-way through a sweep must not let stale results overwrite
    // the new account's list.
    std::uint64_t mGeneration = 0;
};
