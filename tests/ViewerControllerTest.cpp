#include "qml/ViewerController.h"

#include "MockMegaClient.h"
#include "TestZip.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Fixture
{
    std::shared_ptr<MockMegaClient> client = std::make_shared<MockMegaClient>();
    ViewerController controller{client};
    // Stands in for the viewer window each browser is parented to.
    QObject window;
};

// invokeOnGuiThread posts even from the GUI thread, and the directory read is only
// issued once the tail read has landed -- so two turns of the loop per listing.
void drainEvents()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
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

QStringList names(const ArchiveBrowser& browser)
{
    QStringList out;
    for (const QVariant& row : browser.entries())
        out.append(row.toMap().value("name").toString());
    return out;
}

const std::vector<char>& nestedZip()
{
    // "docs" and "docs/img" have no entries of their own; the browser has to
    // synthesise both from the file paths inside them.
    static const std::vector<char> file = testzip::buildZip(
        {{"readme.txt", 500}, {"docs/guide.txt", 1434}, {"docs/img/logo.png", 20}, {"src/", 0}});
    return file;
}

} // namespace

TEST(ViewerControllerTest, ViewerKindNamesTheViewerThatOpensTheFile)
{
    Fixture f;
    EXPECT_EQ(f.controller.viewerKind("photo.JPG"), QStringLiteral("image"));
    EXPECT_EQ(f.controller.viewerKind("raw.cr2"), QStringLiteral("image"));
    EXPECT_EQ(f.controller.viewerKind("clip.MP4"), QStringLiteral("video"));
    EXPECT_EQ(f.controller.viewerKind("clip.mkv"), QStringLiteral("video"));
    EXPECT_EQ(f.controller.viewerKind("manual.PDF"), QStringLiteral("pdf"));
    EXPECT_EQ(f.controller.viewerKind("song.MP3"), QStringLiteral("audio"));
    EXPECT_EQ(f.controller.viewerKind("lossless.flac"), QStringLiteral("audio"));
    EXPECT_EQ(f.controller.viewerKind("bundle.ZIP"), QStringLiteral("archive"));
    EXPECT_TRUE(f.controller.viewerKind("notes.txt").isEmpty());
    EXPECT_TRUE(f.controller.viewerKind("README").isEmpty());
}

TEST(ViewerControllerTest, SourceUrlPassesTheClientsUrlThrough)
{
    Fixture f;
    EXPECT_CALL(*f.client, streamingUrl(42u))
        .WillOnce(testing::Return(Result<std::string>::ok("http://127.0.0.1:8080/2a/photo.jpg")));

    EXPECT_EQ(f.controller.sourceUrl(42u), QStringLiteral("http://127.0.0.1:8080/2a/photo.jpg"));
}

TEST(ViewerControllerTest, SourceUrlIsEmptyWhenTheServerWillNotStart)
{
    Fixture f;
    EXPECT_CALL(*f.client, streamingUrl(42u))
        .WillOnce(testing::Return(Result<std::string>::fail("server not started", -1)));

    EXPECT_TRUE(f.controller.sourceUrl(42u).isEmpty());
}

TEST(ViewerControllerTest, AnArchiveOpensAtItsRootWithFoldersFirst)
{
    Fixture f;
    serveRanges(f, nestedZip());

    ArchiveBrowser* browser = f.controller.openArchive(7, nestedZip().size(), &f.window);
    ASSERT_NE(browser, nullptr);
    EXPECT_EQ(browser->parent(), &f.window);
    EXPECT_EQ(browser->state(), ArchiveBrowser::Loading);

    drainEvents();

    EXPECT_EQ(browser->state(), ArchiveBrowser::Ready);
    EXPECT_TRUE(browser->path().isEmpty());
    // Only the root's own children: nothing from inside docs/ leaks up.
    EXPECT_EQ(names(*browser), (QStringList{"docs", "src", "readme.txt"}));
    const QVariantMap file = browser->entries().at(2).toMap();
    EXPECT_FALSE(file.value("isDirectory").toBool());
    EXPECT_EQ(file.value("formattedSize").toString(), QStringLiteral("500 bytes"));
}

TEST(ViewerControllerTest, TheBrowserWalksDownAndBackUpAFolderAtATime)
{
    Fixture f;
    serveRanges(f, nestedZip());
    ArchiveBrowser* browser = f.controller.openArchive(7, nestedZip().size(), &f.window);
    drainEvents();

    browser->openFolder(QStringLiteral("docs"));
    EXPECT_EQ(browser->path(), QStringList{"docs"});
    EXPECT_EQ(names(*browser), (QStringList{"img", "guide.txt"}));

    browser->openFolder(QStringLiteral("img"));
    EXPECT_EQ(browser->path(), (QStringList{"docs", "img"}));
    EXPECT_EQ(names(*browser), QStringList{"logo.png"});

    browser->goUp();
    EXPECT_EQ(browser->path(), QStringList{"docs"});

    // The breadcrumb's jump straight back to the archive root.
    browser->openFolder(QStringLiteral("img"));
    browser->goToDepth(0);
    EXPECT_TRUE(browser->path().isEmpty());
    EXPECT_EQ(names(*browser), (QStringList{"docs", "src", "readme.txt"}));

    // Already at the root: nowhere further up to go.
    browser->goUp();
    EXPECT_TRUE(browser->path().isEmpty());
}

TEST(ViewerControllerTest, OpeningAFileOrAMissingNameGoesNowhere)
{
    Fixture f;
    serveRanges(f, nestedZip());
    ArchiveBrowser* browser = f.controller.openArchive(7, nestedZip().size(), &f.window);
    drainEvents();

    browser->openFolder(QStringLiteral("readme.txt"));
    browser->openFolder(QStringLiteral("nope"));
    browser->goToDepth(3);

    EXPECT_TRUE(browser->path().isEmpty());
    EXPECT_EQ(names(*browser), (QStringList{"docs", "src", "readme.txt"}));
}

TEST(ViewerControllerTest, AnEmptyFolderEntryListsNothing)
{
    Fixture f;
    serveRanges(f, nestedZip());
    ArchiveBrowser* browser = f.controller.openArchive(7, nestedZip().size(), &f.window);
    drainEvents();

    browser->openFolder(QStringLiteral("src"));

    EXPECT_EQ(browser->path(), QStringList{"src"});
    EXPECT_TRUE(browser->entries().isEmpty());
}

TEST(ViewerControllerTest, TwoBrowsersOnOneArchiveMoveIndependently)
{
    Fixture f;
    serveRanges(f, nestedZip());
    ArchiveBrowser* first = f.controller.openArchive(7, nestedZip().size(), &f.window);
    ArchiveBrowser* second = f.controller.openArchive(7, nestedZip().size(), &f.window);
    drainEvents();

    first->openFolder(QStringLiteral("docs"));

    EXPECT_EQ(first->path(), QStringList{"docs"});
    EXPECT_TRUE(second->path().isEmpty());
    EXPECT_EQ(second->state(), ArchiveBrowser::Ready);
}

TEST(ViewerControllerTest, AFileTooSmallToHoldAnEocdFailsWithoutARequest)
{
    Fixture f;
    EXPECT_CALL(*f.client, readFileRange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    ArchiveBrowser* browser = f.controller.openArchive(7, 8, &f.window);

    EXPECT_EQ(browser->state(), ArchiveBrowser::Failed);
    EXPECT_EQ(browser->reason(), ArchiveBrowser::Unreadable);
}

TEST(ViewerControllerTest, AnArchiveWithNoEntriesSaysSoRatherThanFailing)
{
    Fixture f;
    const std::vector<char> file = testzip::buildZip({});
    serveRanges(f, file);

    ArchiveBrowser* browser = f.controller.openArchive(7, file.size(), &f.window);
    drainEvents();

    EXPECT_EQ(browser->state(), ArchiveBrowser::Failed);
    EXPECT_EQ(browser->reason(), ArchiveBrowser::Empty);
}

TEST(ViewerControllerTest, AFailedReadSaysTheArchiveCouldNotBeLoaded)
{
    Fixture f;
    EXPECT_CALL(*f.client, readFileRange(7, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce([](std::uint64_t,
                     std::uint64_t,
                     std::uint64_t,
                     std::function<void(Result<std::vector<char>>)> onDone) {
            onDone(Result<std::vector<char>>::fail("network", -3));
        });

    ArchiveBrowser* browser = f.controller.openArchive(7, 4096, &f.window);
    drainEvents();

    EXPECT_EQ(browser->state(), ArchiveBrowser::Failed);
    EXPECT_EQ(browser->reason(), ArchiveBrowser::FetchFailed);
}

TEST(ViewerControllerTest, AWindowClosedMidReadDropsTheRestOfTheListing)
{
    // The tail read is held until the browser has gone, as it would be when the
    // window is closed while the SDK is still fetching.
    Fixture f;
    std::function<void(Result<std::vector<char>>)> heldTail;
    EXPECT_CALL(*f.client, readFileRange(7, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce([&heldTail](std::uint64_t,
                              std::uint64_t,
                              std::uint64_t,
                              std::function<void(Result<std::vector<char>>)> onDone) {
            heldTail = std::move(onDone);
        });

    delete f.controller.openArchive(7, nestedZip().size(), &f.window);
    ASSERT_TRUE(heldTail);
    heldTail(Result<std::vector<char>>::ok(nestedZip()));
    drainEvents();
    // No directory read followed: the WillOnce above would have failed on a second call.
}
