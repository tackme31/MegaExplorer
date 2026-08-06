#include "qml/NotificationController.h"

#include "core/MegaErrorCodes.h"

#include <QObject>
#include <QString>

#include <functional>
#include <gtest/gtest.h>

// The one place an errorCode becomes something QML can phrase, so the mapping
// and the rule about when the SDK's English is allowed through are both worth
// pinning down. No TestApp and no event loop: NotificationController is a bare
// QObject relay and a direct connection runs the lambda inside emit.
namespace
{

using Reason = NotificationController::ErrorReason;

struct Emitted
{
    int calls = 0;
    QString context;
    Reason reason = NotificationController::Unknown;
    QString rawMessage;
};

// Records what a NotificationController hands QML. Takes the notify call as a
// callback so both overloads can be driven through the same recording.
Emitted record(const std::function<void(NotificationController&)>& notify)
{
    NotificationController controller;
    Emitted emitted;
    QObject::connect(&controller,
                     &NotificationController::errorOccurred,
                     &controller,
                     [&emitted](QString context, Reason reason, QString rawMessage) {
                         ++emitted.calls;
                         emitted.context = context;
                         emitted.reason = reason;
                         emitted.rawMessage = rawMessage;
                     });

    notify(controller);

    EXPECT_EQ(emitted.calls, 1);
    return emitted;
}

Emitted notifyWithCode(int errorCode, const QString& errorMessage)
{
    return record([&](NotificationController& controller) {
        controller.notifyError(QStringLiteral("navigation"), errorCode, errorMessage);
    });
}

TEST(NotificationControllerTest, ClassifiesTheCodesQmlCanPhrase)
{
    EXPECT_EQ(notifyWithCode(MegaErrorCode::kENoEnt, QStringLiteral("Not found")).reason,
              NotificationController::NotFound);
    EXPECT_EQ(notifyWithCode(MegaErrorCode::kEAccess, QStringLiteral("Access denied")).reason,
              NotificationController::NoPermission);
    EXPECT_EQ(notifyWithCode(MegaErrorCode::kEAgain, QStringLiteral("Try again")).reason,
              NotificationController::Offline);
}

// The allowlist's whole point (docs/ARCHITECTURE.md, "Collapsing a code to a
// verdict"): a code nobody named must not be absorbed into one of the three
// sentences above. Covers an SDK code that isn't in the list, the -1 an
// unclassified failure gets, and this app's own positive sentinels.
TEST(NotificationControllerTest, LeavesEverythingElseUnknown)
{
    EXPECT_EQ(notifyWithCode(MegaErrorCode::kEExist, QStringLiteral("Already exists")).reason,
              NotificationController::Unknown);
    EXPECT_EQ(notifyWithCode(MegaErrorCode::kEInternal, QStringLiteral("Internal error")).reason,
              NotificationController::Unknown);
    EXPECT_EQ(notifyWithCode(1, QStringLiteral("no stored session")).reason,
              NotificationController::Unknown);
}

// The SDK's English is a fixed table looked up from the code itself, so once
// the code has been classified the string adds nothing -- showing it anyway
// would put untranslated text next to a translated sentence.
TEST(NotificationControllerTest, PassesTheRawMessageOnlyWhenClassificationGaveUp)
{
    EXPECT_TRUE(
        notifyWithCode(MegaErrorCode::kENoEnt, QStringLiteral("Not found")).rawMessage.isEmpty());
    EXPECT_EQ(
        notifyWithCode(MegaErrorCode::kEInternal, QStringLiteral("Internal error")).rawMessage,
        QStringLiteral("Internal error"));
}

// The overload for failures this app rejected before the SDK saw them. It has
// no code to classify and no string worth showing, so QML gets neither -- the
// context alone has to select a fixed sentence.
TEST(NotificationControllerTest, ReportsAContextOnlyFailureWithNothingToShow)
{
    const Emitted emitted = record([](NotificationController& controller) {
        controller.notifyError(QStringLiteral("renameInvalidName"));
    });

    EXPECT_EQ(emitted.context, QStringLiteral("renameInvalidName"));
    EXPECT_EQ(emitted.reason, NotificationController::Unknown);
    EXPECT_TRUE(emitted.rawMessage.isEmpty());
}

} // namespace
