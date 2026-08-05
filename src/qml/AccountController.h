#pragma once
#include "core/AccountService.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>

// QML-facing glue for the account section of the "More" menu, registered as
// the "accountController" context property (main.cpp). Also registered as a
// QML type (QML_ELEMENT/QML_UNCREATABLE) purely so QML can write
// AccountController.Loaded / AccountController.ProI by name instead of magic
// numbers; QML never constructs one.
//
// Fetching is lazy and caller-driven: nothing happens at login, and refresh()
// is called from Menu::onAboutToShow. Profile data (email, avatar, display
// name) is read once per session; storage is re-read on every open, because a
// session is expected to stay open for days and a login-time snapshot would
// go stale. While a re-read is in flight the previous value stays on screen,
// so the loading state is only ever visible on the very first open.
//
// Deliberately has no NotificationController* dependency, unlike most
// controllers in this directory: the only user-visible failure is the storage
// fetch, which has its own inline retry affordance in the menu, and a toast
// would fire at login-time for something the user never asked for. Avatar and
// display-name failures degrade silently by design. Failures are logged only.
class AccountController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the accountController context property")

public:
    // Loading is also the reset value, so there is no separate "never asked"
    // state to render. Safe because the toolbar -- and therefore this menu --
    // only exists while logged in.
    enum StorageState
    {
        Loading,
        Loaded,
        Failed
    };
    Q_ENUM(StorageState)

    // Mirrors AccountPlan.h so QML can switch on names. Unknown covers both
    // "not fetched yet" and any level MEGA adds later.
    enum PlanLevel
    {
        Unknown = -1,
        Free = 0,
        ProI = 1,
        ProII = 2,
        ProIII = 3,
        Lite = 4,
        Starter = 11,
        Basic = 12,
        Essential = 13,
        Business = 100,
        ProFlexi = 101,
        Feature = 99999
    };
    Q_ENUM(PlanLevel)

    Q_PROPERTY(QString email READ email NOTIFY profileChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY profileChanged)
    Q_PROPERTY(QString avatarColor READ avatarColor NOTIFY profileChanged)
    Q_PROPERTY(QString avatarInitial READ avatarInitial NOTIFY profileChanged)
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl NOTIFY profileChanged)

    Q_PROPERTY(StorageState storageState READ storageState NOTIFY storageChanged)
    Q_PROPERTY(qreal storageRatio READ storageRatio NOTIFY storageChanged)
    Q_PROPERTY(QString storageText READ storageText NOTIFY storageChanged)
    Q_PROPERTY(int planLevel READ planLevel NOTIFY storageChanged)

    explicit AccountController(std::shared_ptr<AccountService> service, QObject* parent = nullptr);

    // Called from the menu's onAboutToShow. Loads the profile the first time
    // and re-reads storage every time. Cheap to call repeatedly: a re-read
    // that is already in flight is not duplicated.
    Q_INVOKABLE void refresh();

    // Called on logout. Everything here belongs to the account that was
    // signed in.
    Q_INVOKABLE void reset();

    // Backs the inline retry link, which is only shown after a first load has
    // failed. Re-issues the storage read alone -- the avatar and display name
    // have unconditional fallbacks and no retry affordance.
    Q_INVOKABLE void retryAccountInfo();

    QString email() const;
    QString displayName() const; // empty when the account has no name set
    QString avatarColor() const;
    QString avatarInitial() const;
    QUrl avatarUrl() const; // empty when there is no avatar

    StorageState storageState() const;
    qreal storageRatio() const;  // 0.0-1.0, and 0.0 when the max is unknown
    QString storageText() const; // "12.4 GB / 20.0 GB", empty unless Loaded
    int planLevel() const;

signals:
    void profileChanged();
    void storageChanged();

private:
    void loadProfile();
    void loadAccountInfo();
    // Where this account's avatar JPEG is cached. Keyed by user handle so two
    // accounts can't collide on one file.
    QString computeAvatarPath(std::uint64_t userHandle) const;

    std::shared_ptr<AccountService> mService;

    QString mEmail;
    QString mDisplayName;
    QString mAvatarColor;
    QString mAvatarInitial;
    QUrl mAvatarUrl;

    StorageState mStorageState = Loading;
    std::uint64_t mStorageUsedBytes = 0;
    std::uint64_t mStorageMaxBytes = 0;
    int mPlanLevel = Unknown;

    // Whether the once-per-session profile reads have been started. Stays
    // false if the synchronous identity read failed, so a later open retries.
    bool mProfileLoaded = false;
    // Guards against a second storage read while one is outstanding -- a user
    // reopening the menu quickly, or double-clicking the retry link.
    bool mAccountInfoInFlight = false;
    // Whether a storage read has ever succeeded. Drives two rules: no drop
    // back to Loading on a re-read, and no Failed state once a good value is
    // on screen.
    bool mHasStorageValue = false;
    // Bumped by refresh/reset/retryAccountInfo. A fetch abandoned by a logout
    // can still have a queued callback in flight; it carries the old
    // generation and is dropped, so the previous account's numbers can never
    // land in a new session.
    std::uint64_t mGeneration = 0;
};
