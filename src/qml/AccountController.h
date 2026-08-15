#pragma once
#include "core/AccountService.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>

// QML-facing glue for the account section of the "More" menu. Registered as a QML
// type purely so QML can write AccountController.Loaded by name; QML never
// constructs one.
//
// Fetching is lazy and caller-driven: nothing happens at login, refresh() comes
// from the menu's onAboutToShow. Profile data is read once per session; storage is
// re-read on every open, since a session may stay open for days. The previous
// value stays on screen during a re-read, so the loading state shows only once.
//
// fileVersioningEnabled is the exception, and deliberately so: it is read once at
// login from the composition root, because the conflict dialog that words itself
// from it must be right the first time it opens, and nothing gives it a round-trip
// to wait for.
//
// Deliberately has no NotificationController dependency: the only user-visible
// failure is the storage fetch, which has its own inline retry, and a toast would
// fire for something the user never asked for.
class AccountController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the accountController context property")

public:
    // Loading is also the reset value, so there is no separate "never asked" state
    // to render -- safe because the menu only exists while logged in.
    enum StorageState
    {
        Loading,
        Loaded,
        Failed
    };
    Q_ENUM(StorageState)

    // Mirrors AccountPlan.h so QML can switch on names. Unknown covers both "not
    // fetched yet" and any level MEGA adds later.
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

    // True until the account is known to have versioning off: an overwritten file's
    // previous content survives as a version. What CopyConflictDialog words its
    // "Continue" line and its default button from.
    Q_PROPERTY(
        bool fileVersioningEnabled READ fileVersioningEnabled NOTIFY fileVersioningEnabledChanged)

    explicit AccountController(std::shared_ptr<AccountService> service, QObject* parent = nullptr);

    // Cheap to call repeatedly: a re-read already in flight is not duplicated.
    Q_INVOKABLE void refresh();

    // Called on logout: everything here belongs to the account that was signed in.
    Q_INVOKABLE void reset();

    // Re-issues the storage read alone -- avatar and display name have unconditional
    // fallbacks and no retry affordance.
    Q_INVOKABLE void retryAccountInfo();

    // Not Q_INVOKABLE: called once per login from the composition root, like
    // AuthController::restoreSession. Nothing in QML has a reason to re-ask.
    void loadFileVersioning();

    QString email() const;
    QString displayName() const; // empty when the account has no name set
    QString avatarColor() const;
    QString avatarInitial() const;
    QUrl avatarUrl() const; // empty when there is no avatar

    StorageState storageState() const;
    qreal storageRatio() const;  // 0.0-1.0, and 0.0 when the max is unknown
    QString storageText() const; // "12.4 GB / 20.0 GB", empty unless Loaded
    int planLevel() const;
    bool fileVersioningEnabled() const;

signals:
    void profileChanged();
    void storageChanged();
    void fileVersioningEnabledChanged();

private:
    void loadProfile();
    void loadAccountInfo();
    // Keyed by user handle so two accounts can't collide on one cached file.
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
    // Enabled is both the SDK's default for an account that never set the attribute
    // and the safe reading of "not fetched yet": it is the wording that describes
    // what MEGA does unless the user turned versioning off.
    bool mFileVersioningEnabled = true;

    // Stays false if the synchronous identity read failed, so a later open retries.
    bool mProfileLoaded = false;
    // Guards a second storage read while one is outstanding: a menu reopened quickly,
    // or a double-clicked retry link.
    bool mAccountInfoInFlight = false;
    // Once a good value is on screen: never drop back to Loading on a re-read, and
    // never show Failed.
    bool mHasStorageValue = false;
    // A fetch abandoned by a logout can still have a queued callback in flight; it
    // carries the old generation, so the previous account's numbers can't land in a
    // new session.
    std::uint64_t mGeneration = 0;
};
