#include "qml/PreviewController.h"

#include "core/PreviewKind.h"
#include "core/PreviewService.h"
#include "MockMegaClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <functional>
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

void putU16(std::vector<char>& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void putU32(std::vector<char>& out, std::uint32_t value)
{
    putU16(out, static_cast<std::uint16_t>(value & 0xFFFF));
    putU16(out, static_cast<std::uint16_t>((value >> 16) & 0xFFFF));
}

struct ArchiveEntrySpec
{
    std::string name;
    std::uint32_t uncompressed = 0;
    bool utf8 = false;
};

// A central directory and its EOCD, with filler where the local headers would be:
// the listing path never reads those. Deliberately not shared with the builder in
// ZipListingTest.cpp -- what these tests exercise is the tree the controller folds
// the entries into, so the archive only has to be well-formed enough to parse.
std::vector<char> buildZip(const std::vector<ArchiveEntrySpec>& entries)
{
    std::vector<char> directory;
    for (const ArchiveEntrySpec& entry : entries)
    {
        putU32(directory, 0x02014b50);
        putU16(directory, 20);                      // version made by
        putU16(directory, 20);                      // version needed
        putU16(directory, entry.utf8 ? 0x0800 : 0); // general purpose flags
        putU16(directory, 8);                       // deflate
        putU32(directory, 0);                       // modified time and date
        putU32(directory, 0);                       // crc-32
        putU32(directory, entry.uncompressed);      // compressed size
        putU32(directory, entry.uncompressed);      // uncompressed size
        putU16(directory, static_cast<std::uint16_t>(entry.name.size()));
        putU16(directory, 0); // extra length
        putU16(directory, 0); // comment length
        putU16(directory, 0); // disk number start
        putU16(directory, 0); // internal attributes
        putU32(directory, 0); // external attributes
        putU32(directory, 0); // local header offset
        directory.insert(directory.end(), entry.name.begin(), entry.name.end());
    }

    std::vector<char> file(64, 'x');
    const auto directoryAt = static_cast<std::uint32_t>(file.size());
    file.insert(file.end(), directory.begin(), directory.end());
    putU32(file, 0x06054b50);
    putU16(file, 0); // this disk
    putU16(file, 0); // disk the directory starts on
    putU16(file, static_cast<std::uint16_t>(entries.size()));
    putU16(file, static_cast<std::uint16_t>(entries.size()));
    putU32(file, static_cast<std::uint32_t>(directory.size()));
    putU32(file, directoryAt);
    putU16(file, 0); // comment length
    return file;
}

// Both of the listing's range reads land here; each is answered from the same bytes.
void serveRanges(Fixture& f, const std::vector<char>& file)
{
    EXPECT_CALL(*f.client, readFileRange(7, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly([&file](std::uint64_t,
                                std::uint64_t offset,
                                std::uint64_t length,
                                std::function<void(Result<std::vector<char>>)> onDone) {
            const auto from = static_cast<std::ptrdiff_t>(offset);
            const auto count =
                static_cast<std::ptrdiff_t>(std::min<std::uint64_t>(length, file.size() - offset));
            onDone(Result<std::vector<char>>::ok(
                std::vector<char>(file.begin() + from, file.begin() + from + count)));
        });
}

QVariantMap rowAt(const PreviewController& controller, int index)
{
    return controller.archiveEntries().at(index).toMap();
}

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

// Audio became its own PreviewKind so the in-app viewer could open it; the pane has
// nothing new to show, since the server generates no preview for it.
TEST(PreviewControllerTest, AudioStaysUnsupportedInThePane)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPreview(::testing::_, ::testing::_, ::testing::_)).Times(0);

    f.controller.showSelection(1, QStringLiteral("song.mp3"), 4096, false);

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

TEST(PreviewControllerTest, ArchiveEntriesBecomeATreeWithFoldersFirst)
{
    // "docs" has no entry of its own, so the folder row exists only because the
    // controller synthesises it from the path of the file inside it. The name is
    // UTF-8 with the flag set, which is what a modern writer produces.
    Fixture f;
    const std::vector<char> file =
        buildZip({{"docs/\xe3\x83\xa1\xe3\x83\xa2.txt", 1434, true}, {"readme.txt", 500, false}});
    serveRanges(f, file);

    f.controller.showSelection(7, QStringLiteral("bundle.zip"), file.size(), false);
    drainEvents();
    drainEvents(); // the directory read is only issued once the tail read has landed

    EXPECT_EQ(f.controller.state(), PreviewController::Ready);
    EXPECT_EQ(f.controller.kind(), PreviewController::Archive);
    ASSERT_EQ(f.controller.archiveEntries().size(), 3);

    const QVariantMap folder = rowAt(f.controller, 0);
    EXPECT_EQ(folder.value("name").toString(), QStringLiteral("docs"));
    EXPECT_EQ(folder.value("depth").toInt(), 0);
    EXPECT_TRUE(folder.value("isDirectory").toBool());
    EXPECT_EQ(folder.value("formattedSize").toString(), QString());

    const QVariantMap nested = rowAt(f.controller, 1);
    EXPECT_EQ(nested.value("name").toString(), QString::fromUtf8("\xe3\x83\xa1\xe3\x83\xa2.txt"));
    EXPECT_EQ(nested.value("depth").toInt(), 1);
    EXPECT_FALSE(nested.value("isDirectory").toBool());
    // The wording the file list and the properties dialog use, not a raw byte count.
    EXPECT_EQ(nested.value("formattedSize").toString(), QStringLiteral("1.4 kB"));

    // The file at the root sorts after the folder, matching the file views.
    const QVariantMap root = rowAt(f.controller, 2);
    EXPECT_EQ(root.value("name").toString(), QStringLiteral("readme.txt"));
    EXPECT_EQ(root.value("depth").toInt(), 0);
}

TEST(PreviewControllerTest, AnArchiveWithNoEntriesSaysSoRatherThanFailing)
{
    Fixture f;
    const std::vector<char> file = buildZip({});
    serveRanges(f, file);

    f.controller.showSelection(7, QStringLiteral("empty.zip"), file.size(), false);
    drainEvents();

    EXPECT_EQ(f.controller.state(), PreviewController::Unsupported);
    EXPECT_EQ(f.controller.reason(), PreviewController::ArchiveEmpty);
}

TEST(PreviewControllerTest, AFileTooSmallToHoldAnEocdIsRefusedWithoutARequest)
{
    Fixture f;
    EXPECT_CALL(*f.client, readFileRange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    f.controller.showSelection(7, QStringLiteral("stub.zip"), 8, false);

    EXPECT_EQ(f.controller.state(), PreviewController::Unsupported);
    EXPECT_EQ(f.controller.reason(), PreviewController::ArchiveUnreadable);
}
