#include "qml/DownloadController.h"

#include "core/DownloadService.h"
#include "MockMegaClient.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

// Receives QDesktopServices::openUrl in place of the OS. Registering a handler
// for the "file" scheme is what keeps the openFile test from launching a real
// application; it also means openUrl cannot return false while registered, so
// openFile's failure branch (qCWarning + notifyError) stays unverified.
//
// At file scope rather than in the anonymous namespace below because it needs
// moc (see the .moc include at the bottom).
class FileUrlHandler : public QObject
{
    Q_OBJECT

public:
    QList<QUrl> received;

public slots:
    void handle(const QUrl& url)
    {
        received.append(url);
    }
};

namespace
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;

using ProgressCallback = std::function<void(std::uint64_t, std::uint64_t)>;
using DoneCallback = std::function<void(Result<DownloadOutcome>)>;

// Both service->controller callbacks hop through invokeOnGuiThread
// (src/qml/GuiThread.h), and the completion path posts a second time when it
// calls refreshActiveJob after emitting -- so one drain is not always enough.
void flush()
{
    flushQueuedEvents();
    flushQueuedEvents();
}

Result<DownloadOutcome> saved(const char* localPath)
{
    return Result<DownloadOutcome>::ok(DownloadOutcome{localPath});
}

class DownloadControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        client = std::make_shared<MockMegaClient>();
        // download() captures its arguments instead of invoking either
        // callback, so a started job stays Active until a test fires onDone
        // itself. Cardinality is deliberately open: the duplicate-suppression
        // tests are *about* the call count, so they assert on transferCalls
        // where the failure message names what was actually being tested.
        EXPECT_CALL(*client, download(_, _, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](std::uint64_t,
                                          const std::string& destination,
                                          ProgressCallback progress,
                                          DoneCallback done) {
                ++transferCalls;
                destinations.push_back(destination);
                onProgress = std::move(progress);
                onDone = std::move(done);
            }));

        service = std::make_shared<DownloadService>(client);
        notifications = std::make_unique<NotificationController>();
        controller = std::make_unique<DownloadController>(service, notifications.get());

        QObject::connect(controller.get(),
                         &DownloadController::downloadFinished,
                         controller.get(),
                         [this](bool success, QString fileName, QString localPath) {
                             ++finishedCalls;
                             lastSuccess = success;
                             lastFileName = fileName;
                             lastLocalPath = localPath;
                         });
        QObject::connect(
            controller.get(), &DownloadController::downloadActiveChanged, controller.get(), [this] {
                ++activeChanges;
            });
        QObject::connect(notifications.get(),
                         &NotificationController::errorOccurred,
                         notifications.get(),
                         [this](QString, NotificationController::ErrorReason, QString) {
                             ++errorCalls;
                         });
    }

    // Copy before firing: DownloadService starts the next queued job from
    // inside this call, which reassigns the very member being invoked.
    void finish(Result<DownloadOutcome> result)
    {
        DoneCallback done = onDone;
        ASSERT_TRUE(static_cast<bool>(done));
        done(std::move(result));
        flush();
    }

    void deliverProgress(std::uint64_t transferred, std::uint64_t total)
    {
        ProgressCallback tick = onProgress;
        ASSERT_TRUE(static_cast<bool>(tick));
        tick(transferred, total);
        flush();
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<DownloadService> service;
    std::unique_ptr<NotificationController> notifications;
    std::unique_ptr<DownloadController> controller;

    int transferCalls = 0;
    std::vector<std::string> destinations;
    ProgressCallback onProgress; // from the most recent download() call
    DoneCallback onDone;

    int finishedCalls = 0;
    bool lastSuccess = false;
    QString lastFileName;
    QString lastLocalPath;
    int activeChanges = 0;
    int errorCalls = 0;
};

// --- duplicate suppression (DownloadController.h's "No-ops if handle is
// already queued or active") ------------------------------------------------

TEST_F(DownloadControllerTest, SecondRequestForAnActiveHandleIsIgnored)
{
    controller->downloadFile(7, "a.txt", 100);
    controller->downloadFile(7, "a.txt", 100);

    EXPECT_EQ(transferCalls, 1);
    EXPECT_EQ(service->jobs().size(), 1u);
}

TEST_F(DownloadControllerTest, SecondRequestForAQueuedHandleIsIgnored)
{
    controller->downloadFile(1, "a.txt", 0); // starts
    controller->downloadFile(2, "b.txt", 0); // waits behind it
    controller->downloadFile(2, "b.txt", 0);

    EXPECT_EQ(service->jobs().size(), 2u);
    EXPECT_EQ(transferCalls, 1); // job 2 has not been handed to the SDK yet
}

// The guard keys on handle, not name: two different nodes may legitimately
// share a name, and suppressing the second would silently drop a download.
TEST_F(DownloadControllerTest, DifferentHandlesWithTheSameNameBothEnqueue)
{
    controller->downloadFile(1, "a.txt", 0);
    controller->downloadFile(2, "a.txt", 0);

    EXPECT_EQ(service->jobs().size(), 2u);
}

TEST_F(DownloadControllerTest, TheSameHandleCanBeDownloadedAgainAfterItFinished)
{
    controller->downloadFile(7, "a.txt", 0);
    finish(saved("C:\\Downloads\\a.txt"));

    controller->downloadFile(7, "a.txt", 0);

    EXPECT_EQ(transferCalls, 2);
}

// --- downloadFinished's fields ---------------------------------------------

TEST_F(DownloadControllerTest, SuccessReportsTheSavedLeafNameNotTheRequestedName)
{
    controller->downloadFile(1, "photo.jpg", 0);

    // A collision with a different-content local file makes the SDK save under
    // a numbered name; echoing "photo.jpg" here would read as an overwrite.
    finish(saved("C:\\Downloads\\photo (1).jpg"));

    ASSERT_EQ(finishedCalls, 1);
    EXPECT_TRUE(lastSuccess);
    EXPECT_EQ(lastFileName, QStringLiteral("photo (1).jpg"));
}

TEST_F(DownloadControllerTest, SuccessForwardsTheResolvedPath)
{
    controller->downloadFile(1, "photo.jpg", 0);
    finish(saved("C:\\Downloads\\photo.jpg"));

    ASSERT_EQ(finishedCalls, 1);
    EXPECT_EQ(lastLocalPath, QStringLiteral("C:\\Downloads\\photo.jpg"));
}

// The SDK's "network error" must not appear on the signal at all: the snackbar
// says only "Couldn't download photo.jpg", and the reason lives in the log
// (R5-10). Deleting the errorMessage argument is what this pins.
TEST_F(DownloadControllerTest, FailureKeepsTheRequestedNameAndCarriesNoSdkText)
{
    controller->downloadFile(1, "photo.jpg", 0);
    finish(Result<DownloadOutcome>::fail("network error", 2));

    ASSERT_EQ(finishedCalls, 1);
    EXPECT_FALSE(lastSuccess);
    EXPECT_EQ(lastFileName, QStringLiteral("photo.jpg")); // nothing was saved
    EXPECT_TRUE(lastLocalPath.isEmpty());
}

// The snackbar already shows the failure, so raising a toast on top of it
// would report the same thing twice.
TEST_F(DownloadControllerTest, FailureDoesNotRaiseAnErrorToast)
{
    controller->downloadFile(1, "photo.jpg", 0);
    finish(Result<DownloadOutcome>::fail("network error", 2));

    EXPECT_EQ(errorCalls, 0);
}

// Pins the delivery route every test above depends on: the SDK callback runs
// synchronously here, but the signal only reaches QML after the queued invoke.
TEST_F(DownloadControllerTest, FinishedIsNotEmittedUntilTheQueuedInvokeIsDelivered)
{
    controller->downloadFile(1, "a.txt", 0);

    DoneCallback done = onDone;
    ASSERT_TRUE(static_cast<bool>(done));
    done(saved("C:\\Downloads\\a.txt"));

    EXPECT_EQ(finishedCalls, 0);
    flush();
    EXPECT_EQ(finishedCalls, 1);
}

// --- active job and its properties -----------------------------------------

TEST_F(DownloadControllerTest, NoActiveJobInitially)
{
    EXPECT_FALSE(controller->downloadActive());
    EXPECT_TRUE(controller->activeFileName().isEmpty());
    EXPECT_DOUBLE_EQ(controller->activeProgress(), 0.0);
}

// downloadFile() refreshes directly rather than through the queued invoke --
// it already runs on the GUI thread, and the snackbar has to appear on the
// same turn as the click.
TEST_F(DownloadControllerTest, DownloadFileMakesTheJobActiveSynchronously)
{
    controller->downloadFile(1, "a.txt", 100);

    EXPECT_TRUE(controller->downloadActive());
    EXPECT_EQ(controller->activeFileName(), QStringLiteral("a.txt"));
    EXPECT_EQ(activeChanges, 1);
}

TEST_F(DownloadControllerTest, ProgressTickUpdatesActiveProgress)
{
    controller->downloadFile(1, "a.txt", 100);
    deliverProgress(50, 100);

    EXPECT_DOUBLE_EQ(controller->activeProgress(), 0.5);
}

TEST_F(DownloadControllerTest, ActiveProgressIsZeroWhileTotalIsUnknown)
{
    controller->downloadFile(1, "a.txt", 0); // no size known from the file list
    EXPECT_DOUBLE_EQ(controller->activeProgress(), 0.0);

    deliverProgress(10, 0); // and the SDK does not know it either yet
    EXPECT_DOUBLE_EQ(controller->activeProgress(), 0.0);
}

TEST_F(DownloadControllerTest, CompletionClearsTheActiveJob)
{
    controller->downloadFile(1, "a.txt", 100);
    finish(saved("C:\\Downloads\\a.txt"));

    EXPECT_FALSE(controller->downloadActive());
    EXPECT_TRUE(controller->activeFileName().isEmpty());
    EXPECT_DOUBLE_EQ(controller->activeProgress(), 0.0);
}

TEST_F(DownloadControllerTest, ActiveJobAdvancesToTheNextQueuedFile)
{
    controller->downloadFile(1, "a.txt", 0);
    controller->downloadFile(2, "b.txt", 0);

    finish(saved("C:\\Downloads\\a.txt"));

    EXPECT_TRUE(controller->downloadActive());
    EXPECT_EQ(controller->activeFileName(), QStringLiteral("b.txt"));
}

// --- destination path ------------------------------------------------------

// safeLocalFileName's own rules are covered by DownloadServiceTest; what is
// fixed here is that computeDestinationPath actually runs the node name
// through it, so a traversal name cannot leave the Downloads directory.
TEST_F(DownloadControllerTest, DestinationPathIsALeafInsideTheDownloadsDirectory)
{
    controller->downloadFile(1, "..\\..\\evil.exe", 0);

    ASSERT_EQ(destinations.size(), 1u);
    const QString path = QString::fromStdString(destinations.front());
    EXPECT_EQ(QFileInfo(path).fileName(), QStringLiteral("evil.exe"));
    EXPECT_EQ(QDir::toNativeSeparators(QFileInfo(path).absolutePath()),
              QDir::toNativeSeparators(
                  QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
    // The string crosses into the SDK's own LocalPath, which splits on '\\'.
    EXPECT_FALSE(path.contains('/'));
}

// --- openFile --------------------------------------------------------------

TEST_F(DownloadControllerTest, OpenFileHandsTheLocalPathToTheDesktopAsAFileUrl)
{
    FileUrlHandler handler;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &handler, "handle");
    const QString localPath = QStringLiteral("C:\\Downloads\\photo (1).jpg");

    controller->openFile(localPath);

    // Before any assertion can abort the test: the handler must not outlive
    // its registration (QDesktopServices::setUrlHandler).
    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));

    ASSERT_EQ(handler.received.size(), 1);
    EXPECT_EQ(handler.received.front(), QUrl::fromLocalFile(localPath));
    EXPECT_EQ(errorCalls, 0);
}

// --- lifetime --------------------------------------------------------------

// ~DownloadController unregisters both observers, which is what keeps the
// service -- it outlives the controller -- from calling into freed memory.
// This fixes the *result* ("nothing arrives after destruction, and the service
// still drains"), not the mechanism: dropping the unregistration is undefined
// behaviour, so this test can only be expected to catch it, not guaranteed to.
TEST_F(DownloadControllerTest, DestroyingTheControllerUnregistersItsObservers)
{
    controller->downloadFile(1, "a.txt", 0);
    DoneCallback done = onDone;
    ASSERT_TRUE(static_cast<bool>(done));

    controller.reset();
    done(saved("C:\\Downloads\\a.txt"));
    flush();

    EXPECT_EQ(finishedCalls, 0);
    EXPECT_FALSE(service->currentJob().has_value());
}

} // namespace

#include "DownloadControllerTest.moc"
