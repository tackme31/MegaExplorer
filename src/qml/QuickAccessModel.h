#pragma once
#include "core/QuickAccessService.h"

#include <QAbstractListModel>
#include <QString>

#include <cstdint>
#include <memory>

class NotificationController;

// The pin list's model. Flat, unlike FolderTreeModel: a pin is a shortcut, never
// expandable. App-lifetime and shared across tabs like that class, so a bare `this`
// capture in async callbacks is safe.
//
// Everything is keyed by handle rather than row: the login-time validation sweep
// drops dangling pins, which shifts every row after them.
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
                              NotificationController* notifications,
                              QObject* parent = nullptr);
    ~QuickAccessModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    // Login: show the persisted pins immediately -- no blank panel -- then sweep
    // asynchronously to follow renames and drop dead ones. Only a definitive "gone"
    // removes anything; an unanswerable pin is kept as-is.
    Q_INVOKABLE void reload();

    // Sign-out: empties the list without touching the store.
    Q_INVOKABLE void reset();

    Q_INVOKABLE bool isPinned(quint64 handle) const;
    Q_INVOKABLE void pin(quint64 handle, const QString& name);
    Q_INVOKABLE void unpin(quint64 handle);

    // toRow is the row the pin should end up on, clamped into range. A no-op move
    // emits nothing, so a drag that ends where it started is free.
    Q_INVOKABLE void move(quint64 handle, int toRow);

    // Checks the target still exists, then reports through activated()/missing(),
    // covering a pin that goes stale *during* a session. Navigation itself stays
    // QML's job -- this model has no idea which tab is active.
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
    NotificationController* mNotifications;

    // Same generation guard as FolderTreeModel's: a sign-out or re-login part-way
    // through a sweep must not let stale results overwrite the new account's list.
    std::uint64_t mGeneration = 0;
};
