#include "AccountController.h"

#include "app/Logging.h"
#include "GuiThread.h"

#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>

#include <utility>

namespace
{

// The avatar fallback's letter. Takes a whole code point, not a single UTF-16
// code unit, so a name starting outside the BMP (an emoji, say) doesn't get
// cut in half into an unrenderable lone surrogate.
QString firstCharacterUpper(const QString& text)
{
    if (text.isEmpty())
        return QString();
    const int length = text.at(0).isHighSurrogate() ? 2 : 1;
    return text.left(length).toUpper();
}

} // namespace

AccountController::AccountController(std::shared_ptr<AccountService> service, QObject* parent)
    : QObject(parent), mService(std::move(service))
{}

void AccountController::refresh()
{
    if (!mProfileLoaded)
        loadProfile();
    loadAccountInfo();
}

void AccountController::reset()
{
    ++mGeneration;

    mEmail.clear();
    mDisplayName.clear();
    mAvatarColor.clear();
    mAvatarInitial.clear();
    // The cached JPEG on disk is deliberately left alone: it is keyed by user handle,
    // so it cannot reach the wrong account. Clearing the URL is what stops a fast
    // account switch showing the previous user's face.
    mAvatarUrl.clear();
    mProfileLoaded = false;

    mStorageState = Loading;
    mStorageUsedBytes = 0;
    mStorageMaxBytes = 0;
    mPlanLevel = Unknown;
    mAccountInfoInFlight = false;
    mHasStorageValue = false;

    mFileVersioningEnabled = true;

    emit profileChanged();
    emit storageChanged();
    emit fileVersioningEnabledChanged();
}

void AccountController::retryAccountInfo()
{
    loadAccountInfo();
}

void AccountController::loadProfile()
{
    const Result<AccountIdentity> identity = mService->identity();
    if (!identity.success)
    {
        // Leaves mProfileLoaded false so the next open tries again.
        qCWarning(lcAccount) << "account identity unavailable:"
                             << QString::fromStdString(identity.errorMessage);
        return;
    }

    mProfileLoaded = true;
    mEmail = QString::fromStdString(identity.value().email);
    mAvatarColor = QString::fromStdString(identity.value().avatarColor);
    mAvatarInitial = firstCharacterUpper(mEmail);
    emit profileChanged();

    const std::uint64_t generation = mGeneration;

    mService->loadDisplayName([this, generation](std::string name) {
        invokeOnGuiThread(this, [this, generation, name = std::move(name)]() {
            if (generation != mGeneration)
                return;
            mDisplayName = QString::fromStdString(name);
            // The initial follows the name once there is
            // one; the email is only the fallback.
            if (!mDisplayName.isEmpty())
                mAvatarInitial = firstCharacterUpper(mDisplayName);
            emit profileChanged();
        });
    });

    mService->loadAvatar(
        computeAvatarPath(identity.value().userHandle).toStdString(),
        [this, generation](Result<AvatarOutcome> result) {
            invokeOnGuiThread(this, [this, generation, result = std::move(result)]() {
                if (generation != mGeneration)
                    return;
                if (!result.value().hasAvatar)
                {
                    // Expected for most accounts -- the
                    // coloured initial covers it. Logged at
                    // info so the real error code is on
                    // record without looking like a fault.
                    qCInfo(lcAccount) << "no avatar for this account:"
                                      << QString::fromStdString(result.value().errorMessage)
                                      << "code=" << result.value().errorCode;
                    return;
                }
                mAvatarUrl = QUrl::fromLocalFile(QString::fromStdString(result.value().localPath));
                emit profileChanged();
            });
        });
}

void AccountController::loadAccountInfo()
{
    if (mAccountInfoInFlight)
        return;
    mAccountInfoInFlight = true;

    // Only the very first read shows the loading state; a re-read keeps the
    // previous numbers on screen until the new ones arrive.
    if (!mHasStorageValue)
    {
        mStorageState = Loading;
        emit storageChanged();
    }

    const std::uint64_t generation = mGeneration;

    mService->loadAccountInfo([this, generation](Result<AccountInfo> result) {
        invokeOnGuiThread(this, [this, generation, result = std::move(result)]() {
            if (generation != mGeneration)
                return;
            mAccountInfoInFlight = false;

            if (result.success)
            {
                mStorageUsedBytes = result.value().storageUsedBytes;
                mStorageMaxBytes = result.value().storageMaxBytes;
                mPlanLevel = result.value().proLevel;
                mHasStorageValue = true;
                mStorageState = Loaded;
            }
            else if (mHasStorageValue)
            {
                // Keep showing the last good numbers --
                // replacing them with an error would be
                // worse than showing something slightly
                // stale.
                qCWarning(lcAccount)
                    << "account details refresh failed, keeping previous "
                       "value:"
                    << QString::fromStdString(result.errorMessage) << "code=" << result.errorCode;
                return;
            }
            else
            {
                qCWarning(lcAccount)
                    << "account details fetch failed:"
                    << QString::fromStdString(result.errorMessage) << "code=" << result.errorCode;
                mStorageState = Failed;
            }
            emit storageChanged();
        });
    });
}

void AccountController::loadFileVersioning()
{
    const std::uint64_t generation = mGeneration;

    mService->loadFileVersioningEnabled([this, generation](bool enabled) {
        invokeOnGuiThread(this, [this, generation, enabled]() {
            if (generation != mGeneration)
                return;
            if (mFileVersioningEnabled == enabled)
                return;
            mFileVersioningEnabled = enabled;
            emit fileVersioningEnabledChanged();
        });
    });
}

QString AccountController::computeAvatarPath(std::uint64_t userHandle) const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/MegaExplorerAvatars";
    QDir().mkpath(dir);
    // Native separators are required, not cosmetic: the SDK's localpath.cpp
    // splits on '\' on Windows. Same rule as ThumbnailController's path.
    return QDir::toNativeSeparators(dir + "/" + QString::number(userHandle) + ".jpg");
}

QString AccountController::email() const
{
    return mEmail;
}

QString AccountController::displayName() const
{
    return mDisplayName;
}

QString AccountController::avatarColor() const
{
    return mAvatarColor;
}

QString AccountController::avatarInitial() const
{
    return mAvatarInitial;
}

QUrl AccountController::avatarUrl() const
{
    return mAvatarUrl;
}

AccountController::StorageState AccountController::storageState() const
{
    return mStorageState;
}

qreal AccountController::storageRatio() const
{
    // Business and Pro Flexi accounts can report an unknown maximum; without
    // this guard QML would get a NaN width binding.
    if (mStorageMaxBytes == 0)
        return 0.0;
    const qreal ratio =
        static_cast<qreal>(mStorageUsedBytes) / static_cast<qreal>(mStorageMaxBytes);
    return ratio > 1.0 ? 1.0 : ratio;
}

QString AccountController::storageText() const
{
    if (mStorageState != Loaded)
        return QString();

    // Formatted here because QML has no formattedDataSize equivalent; both halves
    // share one base so the numbers stay comparable.
    //
    // Traditional (1024-based, "GB"-labelled) rather than SI, because that is what
    // MEGA itself quotes: 16106127360 bytes is "15 GB" on MEGA's own site, not SI's
    // 16.1 GB.
    // c() not system(): the unit word is locale data, and the UI is English-only.
    const QLocale locale = QLocale::c();
    const QString used = locale.formattedDataSize(
        static_cast<qint64>(mStorageUsedBytes), 1, QLocale::DataSizeTraditionalFormat);
    if (mStorageMaxBytes == 0)
        return used;
    return QStringLiteral("%1 / %2").arg(
        used,
        locale.formattedDataSize(
            static_cast<qint64>(mStorageMaxBytes), 1, QLocale::DataSizeTraditionalFormat));
}

int AccountController::planLevel() const
{
    return mPlanLevel;
}

bool AccountController::fileVersioningEnabled() const
{
    return mFileVersioningEnabled;
}
