#include "qml/PreviewController.h"

#include "core/PreviewKind.h"
#include "core/PreviewService.h"
#include "MockMegaClient.h"

#include <QCoreApplication>
#include <QFile>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

// invokeOnGuiThread posts queued even when the callback already runs on the GUI
// thread, so nothing this controller publishes is visible until the loop turns.
void drainEvents()
{
    QCoreApplication::processEvents();
}

void writeFile(const std::string& path, const QByteArray& bytes)
{
    QFile file(QString::fromStdString(path));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
}

struct Fixture
{
    std::shared_ptr<MockMegaClient> client = std::make_shared<MockMegaClient>();
    std::shared_ptr<PreviewService> service = std::make_shared<PreviewService>(client);
    std::shared_ptr<PreviewImageStore> store = std::make_shared<PreviewImageStore>();
    PreviewController controller{service, store};
};

} // namespace

TEST(PreviewControllerTest, FolderSelectionGoesEmptyWithoutTouchingTheClient)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPreview(::testing::_, ::testing::_, ::testing::_)).Times(0);

    f.controller.showSelection(1, QStringLiteral("Holiday photos"), 0, true);

    EXPECT_EQ(f.controller.state(), PreviewController::Empty);
}

TEST(PreviewControllerTest, UnsupportedExtensionNeverReachesTheClient)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPreview(::testing::_, ::testing::_, ::testing::_)).Times(0);

    f.controller.showSelection(1, QStringLiteral("installer.exe"), 4096, false);

    EXPECT_EQ(f.controller.state(), PreviewController::Unsupported);
    EXPECT_EQ(f.controller.reason(), PreviewController::UnsupportedType);
}

TEST(PreviewControllerTest, ImageArrivalPublishesReadyImageAndRemovesTheTempFile)
{
    Fixture f;
    std::string writtenPath;
    EXPECT_CALL(*f.client, getPreview(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([&](std::uint64_t,
                                        const std::string& path,
                                        std::function<void(Result<std::string>)> onDone) {
            // Stands in for the SDK writing the preview attribute to disk.
            writtenPath = path;
            writeFile(path, QByteArray("not-really-a-jpeg"));
            onDone(Result<std::string>::ok(path));
        }));

    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Ready);
    EXPECT_EQ(f.controller.kind(), PreviewController::Image);
    EXPECT_TRUE(f.controller.imageSource().startsWith(QStringLiteral("image://megapreview/")));
    // The bytes moved into the store, and nothing was left behind on disk.
    EXPECT_FALSE(QFile::exists(QString::fromStdString(writtenPath)));
}

TEST(PreviewControllerTest, RepeatingTheSameSelectionDoesNotRefetch)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPreview(7, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([](std::uint64_t,
                                       const std::string& path,
                                       std::function<void(Result<std::string>)> onDone) {
            writeFile(path, QByteArray("not-really-a-jpeg"));
            onDone(Result<std::string>::ok(path));
        }));

    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
    drainEvents();
    const QString firstSource = f.controller.imageSource();
    // Clicking the already-selected row re-emits the selection.
    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Ready);
    EXPECT_EQ(f.controller.imageSource(), firstSource);

    // Hiding the pane and coming back does fetch again -- clear() dropped the image.
    EXPECT_CALL(*f.client, getPreview(7, ::testing::_, ::testing::_)).Times(1);
    f.controller.clear();
    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
}

TEST(PreviewControllerTest, SupersededArrivalIsDiscardedAndItsFileDeleted)
{
    // The regression guard for the generation counter: getPreview cannot be
    // cancelled, so a request abandoned by a cursor move still lands, and both its
    // result and its file have to be dropped rather than shown.
    Fixture f;
    std::vector<std::string> paths;
    std::function<void(Result<std::string>)> firstOnDone;
    EXPECT_CALL(*f.client, getPreview(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke([&](std::uint64_t,
                                              const std::string& path,
                                              std::function<void(Result<std::string>)> onDone) {
            paths.push_back(path);
            writeFile(path, QByteArray("not-really-a-jpeg"));
            if (paths.size() == 1)
                firstOnDone = std::move(onDone);
        }));

    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
    ASSERT_EQ(paths.size(), 1u);
    // The cursor moves on before the first result lands.
    f.controller.showSelection(8, QStringLiteral("other.jpg"), 200000, false);

    // Act
    ASSERT_TRUE(static_cast<bool>(firstOnDone));
    firstOnDone(Result<std::string>::ok(paths[0]));
    drainEvents();

    // Assert: the stale result never became visible, and its file is gone
    EXPECT_EQ(f.controller.state(), PreviewController::Loading);
    EXPECT_FALSE(QFile::exists(QString::fromStdString(paths[0])));
}

TEST(PreviewControllerTest, FetchFailureLandsInUnsupportedWithoutNotifying)
{
    // A previewable type whose node has no stored preview is the normal case, not
    // an error -- which is why this controller takes no NotificationController and
    // there is nothing here to assert a toast against.
    Fixture f;
    EXPECT_CALL(*f.client, getPreview(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::string>::fail("no preview", -9)));

    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Unsupported);
    EXPECT_EQ(f.controller.reason(), PreviewController::NoPreviewAvailable);
}

TEST(PreviewControllerTest, ClearGoesEmptyAndInvalidatesAnythingInFlight)
{
    Fixture f;
    std::string writtenPath;
    std::function<void(Result<std::string>)> onDone;
    EXPECT_CALL(*f.client, getPreview(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([&](std::uint64_t,
                                        const std::string& path,
                                        std::function<void(Result<std::string>)> callback) {
            writtenPath = path;
            writeFile(path, QByteArray("not-really-a-jpeg"));
            onDone = std::move(callback);
        }));

    f.controller.showSelection(7, QStringLiteral("photo.jpg"), 200000, false);
    f.controller.clear();

    ASSERT_TRUE(static_cast<bool>(onDone));
    onDone(Result<std::string>::ok(writtenPath));
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Empty);
    EXPECT_FALSE(QFile::exists(QString::fromStdString(writtenPath)));
}

TEST(PreviewControllerTest, TextOverTheLimitIsRefusedWithoutARequest)
{
    // sizeBytes came with the folder listing, so a huge log costs no traffic at all
    // to turn down.
    Fixture f;
    EXPECT_CALL(*f.client, readFileContent(::testing::_, ::testing::_, ::testing::_)).Times(0);

    f.controller.showSelection(7, QStringLiteral("huge.log"), kMaxTextPreviewBytes + 1, false);

    EXPECT_EQ(f.controller.state(), PreviewController::Unsupported);
    EXPECT_EQ(f.controller.reason(), PreviewController::TooLarge);
}

TEST(PreviewControllerTest, TextArrivalPublishesReadyText)
{
    Fixture f;
    const std::string contents = "hello\nworld\n";
    EXPECT_CALL(*f.client, readFileContent(7, kMaxTextPreviewBytes, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<char>>::ok(std::vector<char>(contents.begin(), contents.end()))));

    f.controller.showSelection(7, QStringLiteral("notes.txt"), 12, false);
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Ready);
    EXPECT_EQ(f.controller.kind(), PreviewController::Text);
    EXPECT_EQ(f.controller.text(), QStringLiteral("hello\nworld\n"));
}

TEST(PreviewControllerTest, BinaryBytesBehindATextExtensionAreRefused)
{
    // The extension whitelist is a guess; the NUL check is what actually decides.
    Fixture f;
    const std::vector<char> bytes{'M', 'Z', '\0', 'x'};
    EXPECT_CALL(*f.client, readFileContent(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<char>>::ok(bytes)));

    f.controller.showSelection(7, QStringLiteral("payload.txt"), 4, false);
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Unsupported);
    EXPECT_EQ(f.controller.reason(), PreviewController::BinaryContent);
}
